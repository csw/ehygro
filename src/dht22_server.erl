-module(dht22_server).

-behaviour(gen_server).

-export([start_link/0, get_reading/0]).

-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, "dht_interface", []).

init(ExtProg) ->
    process_flag(trap_exit, true),
    Port = open_port({spawn, ExtProg}, [{packet, 2}, binary]),
    {ok, {Port}}.

get_reading() ->
    gen_server:call(?MODULE, reading).

%% callbacks

handle_call(reading, _From, {Port} = State) ->
    case port_req_reply(Port, {read, 10}, 20000) of
        {ok, Res} ->
            {reply, Res, State};
        timeout ->
            {stop, port_timeout, State}
    end.

handle_info({'EXIT', Port, Reason}, {Port} = State) ->
    {stop, {port_terminated, Reason}, State}.

terminate({port_terminated, _Reason}, _State) ->
    ok;
terminate(_Reason, {Port}) ->
    port_close(Port).


port_req_reply(Port, Cmd, Timeout) ->
    true = port_command(Port, term_to_binary(Cmd)),
    receive
        {Port, {data, Data}} ->
            {ok, binary_to_term(Data)}
    after Timeout ->
            timeout
    end.
    


handle_cast(_Msg, State) ->
    {noreply, State}.

code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

