
src_list_cmd = run_command('sh', '-c', 'ls lib/src/*.c', check : true)
srcs = files(src_list_cmd.stdout().strip().split())

inc_pub  = include_directories('../../../ghdr/common')
inc_parser_libs = include_directories('../inc')
inc_common = include_directories('../common')

lib_hevc_parser = shared_library(
  'hevc_parser_arm_elinux',
  srcs,
  include_directories : [inc_pub, inc_parser_libs, inc_common],
  name_prefix         : 'lib_',
  soversion           : '0.1',
  install             : true,
  install_dir         : [libs_install_dir],
)

hevc_parser_dep = declare_dependency(
  include_directories : [inc_parser_libs, inc_common],
  link_with           : lib_hevc_parser,
)

