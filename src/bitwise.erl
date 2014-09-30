%% bitwise: NIF example module showing different NIF scheduling issues
%%
%% The exor function variants here all take a binary and a byte value as
%% arguments and return a binary and either the number of times the
%% scheduler thread was yielded (if known) or the number of chunks of the
%% binary that were processed. The returned binary is the same size as the
%% binary argument, and its value is that of the binary argument with the
%% byte argument xor'd with each byte of the binary. The idea is that if
%% you pass in a large enough binary, you can get bad or good NIF behavior
%% with respect to Erlang scheduler threads depending on which function
%% variant you call, and different calls take different approaches to
%% trying to avoid scheduler collapse and other scheduling problems.
%%
%% This code requires Erlang 17.3 or newer, built with dirty schedulers
%% enabled.
%%
%% This code was originally presented at Chicago Erlang on 22 Sep
%% 2014. Please see the PDF file in this repository for the presentation.
%%
-module(bitwise).
-author('vinoski@ieee.org').
-export([exor_chunks/2]).
-export([exor_bad/2, exor_yield/2, exor_dirty/2, reds/3]).
-on_load(init/0).

%% With a large Bin argument, exor_bad takes far too long for a NIF
exor_bad(Bin, Byte) when is_binary(Bin), Byte >= 0, Byte < 256 ->
    erlang:nif_error({nif_not_loaded, ?MODULE}).

%% exor_yield processes Bin in chunks and uses enif_schedule_nif to yield
%% the scheduler thread between chunks
exor_yield(Bin, Byte) when is_binary(Bin), Byte >= 0, Byte < 256 ->
    erlang:nif_error({nif_not_loaded, ?MODULE}).

%% exor_dirty processes Bin on a dirty scheduler
exor_dirty(Bin, Byte) when is_binary(Bin), Byte >= 0, Byte < 256 ->
    erlang:nif_error({nif_not_loaded, ?MODULE}).

%% Similar to exor_yield but do the chunking in Erlang
exor_chunks(Bin, Byte) when is_binary(Bin), Byte >= 0, Byte < 256 ->
    exor_chunks(Bin, Byte, 4194304, 0, <<>>).
exor_chunks(Bin, Byte, ChunkSize, Yields, Acc) ->
    case byte_size(Bin) of
        Size when Size > ChunkSize ->
            <<Chunk:ChunkSize/binary, Rest/binary>> = Bin,
            {Res,_} = exor_bad(Chunk, Byte),
            exor_chunks(Rest, Byte, ChunkSize,
                        Yields+1, <<Acc/binary, Res/binary>>);
        _ ->
            {Res, _} = exor_bad(Bin, Byte),
            {<<Acc/binary, Res/binary>>, Yields}
    end.

%% Count reductions and number of scheduler yields for Fun. Fun is assumed
%% to be one of the above exor variants.
reds(Bin, Byte, Fun) when is_binary(Bin), Byte >= 0, Byte < 256 ->
    Parent = self(),
    Pid = spawn(fun() ->
                        Self = self(),
                        Start = os:timestamp(),
                        R0 = process_info(Self, reductions),
                        {_,Yields} = Fun(Bin, Byte),
                        R1 = process_info(Self, reductions),
                        T = timer:now_diff(os:timestamp(), Start),
                        Parent ! {Self,{T, Yields, R0, R1}}
                end),
    receive
        {Pid,Result} ->
            Result
    end.

init() ->
    SoName = filename:join(case code:priv_dir(?MODULE) of
                               {error, bad_name} ->
                                   Dir = code:which(?MODULE),
                                   filename:join([filename:dirname(Dir),
                                                  "..", "priv"]);
                               Dir ->
                                   Dir
                           end, atom_to_list(?MODULE) ++ "_nif"),
    erlang:load_nif(SoName, 0).
