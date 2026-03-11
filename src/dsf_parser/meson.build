
src_list_cmd = run_command('sh', '-c', 'ls lib/src/*.c', check : true)
srcs = files(src_list_cmd.stdout().strip().split())

inc_pub  = include_directories('../../ghdr/common')
inc_pub_api  = include_directories('../../ghdr')
inc_priv = include_directories('lib/inc')
id3_inc = include_directories('../parser_libs/inc')

lib_dsf = shared_library(
  'dsf_parser_arm_elinux',
  srcs,
  include_directories : [inc_pub, inc_pub_api, inc_priv, id3_inc],
  name_prefix         : 'lib_',
  soversion           : '3.1',
  dependencies        : [id3_parser_dep],
  c_args              : ['-D_LINUX'],
  install             : true,
  install_dir         : [libs_install_dir],
)