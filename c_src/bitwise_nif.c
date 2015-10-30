/*
 * bitwise_nif: examples of NIF scheduling
 */
#include <sys/time.h>
#include "erl_nif.h"

/*
 * The exor functions here take a binary and a byte and generate a new
 * binary by applying xor of the byte value to each byte of the binary. It
 * returns a tuple of the new binary and a count of how many times the
 * Erlang scheduler thread is yielded during processing of the binary.
 */

/*
 * exor misbehaves on a regular scheduler thread when the incomng binary is
 * large because it blocks the thread for too long. But it works fine on a
 * dirty scheduler.
 */
static ERL_NIF_TERM
exor(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin, outbin;
    unsigned char byte;
    unsigned val, i;

    if (argc != 2 || !enif_inspect_binary(env, argv[0], &bin) ||
        !enif_get_uint(env, argv[1], &val) || val > 255)
        return enif_make_badarg(env);
    if (bin.size == 0)
        return argv[0];
    byte = (unsigned char)val;
    enif_alloc_binary(bin.size, &outbin);
    for (i = 0; i < bin.size; i++)
        outbin.data[i] = bin.data[i] ^ byte;
    return enif_make_tuple2(env,
                            enif_make_binary(env, &outbin),
                            enif_make_int(env, 0));
}

/*
 * exor2 is an "internal NIF" scheduled by exor_yield below. It takes the
 * binary and byte arguments, same as the other functions here, but also
 * takes a count of the max number of bytes to process per timeslice, the
 * offset into the binary at which to start processing, the resource type
 * holding the resulting data, and the number of times rescheduling is done
 * via enif_schedule_nif.
 */
static ERL_NIF_TERM
exor2(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    unsigned long offset, i, end, max_per_slice;
    struct timeval start, stop, slice;
    int pct, total = 0, yields;
    ERL_NIF_TERM newargv[6];
    ERL_NIF_TERM result;
    unsigned char byte;
    ErlNifBinary bin;
    unsigned val;
    void* res;

    if (argc != 6 || !enif_inspect_binary(env, argv[0], &bin) ||
        !enif_get_uint(env, argv[1], &val) || val > 255 ||
        !enif_get_ulong(env, argv[2], &max_per_slice) ||
        !enif_get_ulong(env, argv[3], &offset) ||
        !enif_get_resource(env, argv[4], res_type, &res) ||
        !enif_get_int(env, argv[5], &yields))
        return enif_make_badarg(env);
    byte = (unsigned char)val;
    end = offset + max_per_slice;
    if (end > bin.size) end = bin.size;
    i = offset;
    while (i < bin.size) {
        gettimeofday(&start, NULL);
        do {
            ((char*)res)[i] = bin.data[i] ^ byte;
        } while (++i < end);
        if (i == bin.size) break;
        gettimeofday(&stop, NULL);
        /* determine how much of the timeslice was used */
        timersub(&stop, &start, &slice);
        pct = (int)((slice.tv_sec*1000000+slice.tv_usec)/10);
        total += pct;
        if (pct > 100) pct = 100;
        else if (pct == 0) pct = 1;
        if (enif_consume_timeslice(env, pct)) {
            /* the timeslice has been used up, so adjust our max_per_slice byte count based on
             * the processing we've done, then reschedule to run again */
            max_per_slice = i - offset;
            if (total > 100) {
                int m = (int)(total/100);
                if (m == 1)
                    max_per_slice -= (unsigned long)(max_per_slice*(total-100)/100);
                else
                    max_per_slice = (unsigned long)(max_per_slice/m);
            }
            newargv[0] = argv[0];
            newargv[1] = argv[1];
            newargv[2] = enif_make_ulong(env, max_per_slice);
            newargv[3] = enif_make_ulong(env, i);
            newargv[4] = argv[4];
            newargv[5] = enif_make_int(env, yields+1);
            return enif_schedule_nif(env, "exor2", 0, exor2, argc, newargv);
        }
        end += max_per_slice;
        if (end > bin.size) end = bin.size;
    }
    result = enif_make_resource_binary(env, res, res, bin.size);
    return enif_make_tuple2(env, result, enif_make_int(env, yields));
}

/*
 * exor_yield just schedules exor2 for execution, providing an initial
 * guess of 4MB for the max number of bytes to process before yielding the
 * scheduler thread
 */
static ERL_NIF_TERM
exor_yield(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    ERL_NIF_TERM newargv[6];
    ErlNifBinary bin;
    unsigned val;
    void* res;

    if (argc != 2 || !enif_inspect_binary(env, argv[0], &bin) ||
        !enif_get_uint(env, argv[1], &val) || val > 255)
        return enif_make_badarg(env);
    if (bin.size == 0)
        return argv[0];
    newargv[0] = argv[0];
    newargv[1] = argv[1];
    newargv[2] = enif_make_ulong(env, 4194304);
    newargv[3] = enif_make_ulong(env, 0);
    res = enif_alloc_resource(res_type, bin.size);
    newargv[4] = enif_make_resource(env, res);
    newargv[5] = enif_make_int(env, 0);
    enif_release_resource(res);
    return enif_schedule_nif(env, "exor2", 0, exor2, 6, newargv);
}

static int
nifload(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    *priv_data = enif_open_resource_type(env,
                                         NULL,
                                         "bitwise_buf",
                                         NULL,
                                         ERL_NIF_RT_CREATE|ERL_NIF_RT_TAKEOVER,
                                         NULL);
    return 0;
}

static int
nifupgrade(ErlNifEnv* env, void** priv_data, void** old_priv_data, ERL_NIF_TERM load_info)
{
    *priv_data = enif_open_resource_type(env,
                                         NULL,
                                         "bitwise_buf",
                                         NULL,
                                         ERL_NIF_RT_TAKEOVER,
                                         NULL);
    return 0;
}

/*
 * Note that exor, exor_bad, and exor_dirty all run the same C function,
 * but exor and exor_bad run it on a regular scheduler thread whereas
 * exor_dirty runs it on a dirty CPU scheduler thread
 */
static ErlNifFunc funcs[] = {
    {"exor", 2, exor},
    {"exor_bad", 2, exor},
    {"exor_yield", 2, exor_yield},
    {"exor_dirty", 2, exor, ERL_NIF_DIRTY_JOB_CPU_BOUND},
};
ERL_NIF_INIT(bitwise,funcs,nifload,NULL,nifupgrade,NULL)
