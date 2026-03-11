
src_list_cmd = run_command('sh', '-c', 'ls lib/src/*.c', check : true)
srcs = files(src_list_cmd.stdout().strip().split())

inc_pub  = include_directories('../../ghdr/common')
inc_pub_api  = include_directories('../../ghdr')
inc_priv = include_directories('lib/inc')

lib_ogg = shared_library(
  'ogg_parser_arm_elinux',
  srcs,
  include_directories : [inc_pub, inc_pub_api, inc_priv],
  name_prefix         : 'lib_',
  soversion           : '3.1',
  install             : true,
  install_dir         : [libs_install_dir],
)