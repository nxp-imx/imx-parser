
srcs = [
  'src/fsl_osal.c',
  'src/aac.c',
  'src/err_check.c',
  'src/queue_mgr.c',
  'src/memory_mgr.c',
  'src/subtitle.c',
  'src/fsl_parser_log.c',
]

fsl_test_src = ['src/fsl_parser_test.c',]
regression_test_src = ['src/regression_test.c',]

inc_pub  = include_directories('../../ghdr/common')
inc_pub_api  = include_directories('../../ghdr')
inc_priv = include_directories('inc')

fsl_parser_test = executable(
  'fsl_parser_test_arm_elinux',
  [srcs, fsl_test_src],
  include_directories : [inc_pub, inc_pub_api, inc_priv],
  dependencies        : [id3_parser_dep],
  c_args              : ['-D_POSIX_C_SOURCE=200809L', '-DSUPPORT_LARGE_FILE', '-pthread'],
  install             : true,
  install_dir         : ['/usr/bin'],
)

regression_test = executable(
  'regression_test_arm_elinux',
  [srcs, regression_test_src],
  include_directories : [inc_pub, inc_pub_api, inc_priv],
  c_args              : ['-D_POSIX_C_SOURCE=200809L', '-DSUPPORT_LARGE_FILE', '-pthread'],
  dependencies        : [id3_parser_dep],
  install             : true,
  install_dir         : ['/usr/bin'],
)

