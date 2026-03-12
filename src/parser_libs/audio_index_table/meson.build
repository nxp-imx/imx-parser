
src_list_cmd = run_command('sh', '-c', 'ls lib/src/*.c', check : true)
srcs = files(src_list_cmd.stdout().strip().split())

inc_pub  = include_directories('../../../ghdr/common')
inc_priv = include_directories('lib/inc')
inc_parser_libs = include_directories('../inc')

lib_audio_index_table = shared_library(
  'audio_index_table_arm_elinux',
  srcs,
  include_directories : [inc_pub, inc_priv, inc_parser_libs],
  name_prefix         : 'lib_',
  install             : true,
  install_dir         : [libs_install_dir],
)

audio_index_table_dep = declare_dependency(
  include_directories : [inc_parser_libs],
  link_with           : lib_audio_index_table,
)

