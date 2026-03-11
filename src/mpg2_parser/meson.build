
src_list_cmd = run_command('sh', '-c', 'ls lib/src/*.c', check : true)
srcs = files(src_list_cmd.stdout().strip().split())

inc_pub  = include_directories('../../ghdr/common')
inc_pub_api  = include_directories('../../ghdr')
inc_priv = include_directories('lib/inc')

lib_mpg2 = shared_library(
  'mpg2_parser_arm_elinux',
  srcs,
  include_directories : [inc_pub, inc_pub_api, inc_priv],
  name_prefix         : 'lib_',
  soversion           : '3.1',
  dependencies        : [h264_parser_dep, hevc_parser_dep, aac_latm_parser_dep],
  c_args              : ['-D_LINUX'],
  install             : true,
  install_dir         : [libs_install_dir],
)