# bitwise: NIF examples showing Erlang scheduler concerns

The `bitwise` module implements several Erlang Native Implemented Functions
(NIFs) intended to show several different effects NIFs can have on Erlang
scheduler threads. The module supplies several variants of a function
`exor/2` that takes a binary and a byte value and applies *exclusive-or* of
that byte to every byte in the binary and returns a new binary of the
resulting values. These variants operate as follows:

* One example, `exor_bad/2`, shows a misbehaving NIF that, given a large
  enough input binary, takes up far too much time on a scheduler thread,
  running for multiple seconds. Normally, a NIF should run on a scheduler
  thread for only a millisecond or less.

* Another example uses Erlang code to break the large input binary into 4MB
  chunks, calling `exor_bad/2` separately for each chunk and then
  reassembling the results.

* The `exor_yield/2` variant uses the
  [enif_schedule_nif function](http://www.erlang.org/doc/man/erl_nif.html#enif_schedule_nif),
  introduced in Erlang/OTP 17.3, to ensure the NIF yields the scheduler
  thread after consuming a 1 millisecond timeslice. It uses
  `enif_schedule_nif` to reschedule itself to run in the future to continue
  its *exclusive-or* operation on the input binary.

* The final variant, `exor_dirty/2`, uses dirty schedulers, introduced as
  an experimental feature in Erlang 17.0. This approach schedules the NIF
  to run on a dirty scheduler thread rather than a regular scheduler
  thread. Since dirty scheduler threads are not
  [managed threads](https://github.com/erlang/otp/blob/maint/erts/emulator/internal_doc/ThreadProgress.md),
  they are not constrained the same way regular scheduler threads are with
  respect to long-running CPU- or I/O-intensive tasks.

This code was originally presented at Chicago Erlang, 22 Sep
2014. The code has evolved since that talk, including a fix for the example
of how `enif_consume_timeslice()` and `enif_schedule_nif()` are used
together. In the Chicago Erlang presentation, the code presented for this
area miscalculated timeslice percentages; this has been fixed. The slides
have been updated to include this fix as well, which means the slides here,
in the file
[`vinoski-opt-native-code.pdf`](https://github.com/vinoski/bitwise/blob/master/vinoski-opt-native-code.pdf),
differ from those originally presented.

This code was also presented at CodeMesh 2014, 5 Nov 2014. The slides for
that talk, which are in the file
[`vinoski-schedulers.pdf`](https://github.com/vinoski/bitwise/blob/master/vinoski-schedulers.pdf),
include more details than those for the Chicago Erlang talk, specifically
about a possible dirty driver API.

## Long Scheduling

A useful Erlang feature not shown in the code or the Chicago Erlang slides,
but mentioned in the CodeMesh slides, is the ability to detect when native
code spends too much time on a regular scheduler thread by calling
`erlang:system_monitor/2` with the `{long_schedule, Time}` option. For
example, the following code can be interactively run in an Erlang shell to
cause the shell to receive messages when any NIFs execute on a regular
scheduler thread for 10ms or more:

    1> erlang:system_monitor(self(), [{long_schedule, 10}]).
    undefined

If any NIF executions meet or exceed the 10ms limit, the shell will receive
messages similar to the following:

    2> spawn(fun() -> bitwise:exor(LargeBinary, 16#5A) end).
    <0.39.0>
    3> flush().
    Shell got {monitor,<0.39.0>,long_schedule,
                       [{timeout,6018},{in,undefined},{out,undefined}]}

Here, the `{timeout, 6018}` portion of the message shows that
`bitwise:exor/2` executed for slightly more than 6 seconds.

See the
[erlang:system_monitor/2 documentation](http://www.erlang.org/doc/man/erlang.html#system_monitor-2)
for more details.
