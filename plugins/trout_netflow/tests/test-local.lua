

--output_to_file = { file_name = 'my_output_file.txt' }
--output_to_pipe = { pipe_name = '/tmp/llpipe_17115325107265013479' }                                            
--output_to_pipe = { pipe_env = 'pipename' }
--output_to_stdout = {}

--log_lorth = { output = 'output_to_stdout' }
--log_bill = { output = 'output_to_pipe' }
--log_txt = { output = 'output_to_stdout' }


serializer_txt = { }
serializer_lorth = { }

logger_file = { file_name = 'test.txt',
                serializer = 'serializer_txt' }
logger_null = { }
logger_stdout = { serializer = 'serializer_txt' }
logger_pipe = { serializer = 'serializer_txt',
                pipe_name = "/srv/snort-modules/test_pipe" }

alert_lioli = { logger = 'logger_null' }

trout_netflow = { logger = 'logger_pipe'  }                   

stream = {}
stream_tcp = {}
http_inspect = {}

wizard = {
    spells = { { service = 'http', proto = 'tcp', to_server = {'GET'}, to_client = {'HTTP/'} } }
}

binder = {
    { when = { service = 'http' }, use = { type = 'http_inspect' } },
    { use = { type = 'wizard' } }
}

ips = {
  include = 'test-local.rules'
}
