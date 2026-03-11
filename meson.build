project(
  'imx-parser',
  'c',
  version : '5.0.0',
  meson_version : '>=0.60.0',
  default_options : [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
    'werror=true',
  ]
)

pub_header_top = files('ghdr/fsl_parser_api.h')

pub_header_common = [
	'ghdr/common/AudioCoreParser.h',
	'ghdr/common/file_stream.h',
	'ghdr/common/fsl_media_types.h',
	'ghdr/common/fsl_muxer.h',
	'ghdr/common/fsl_parser.h',
	'ghdr/common/fsl_parser_drm.h',
	'ghdr/common/fsl_types.h',
]

libs_install_dir = join_paths(get_option('libdir'), 'imx-mm/parser')

install_headers(pub_header_top, install_dir : '/usr/include/imx-mm/parser')
install_headers(pub_header_common, install_dir : '/usr/include/imx-mm/parser')

subdir('src/parser_libs/id3_parser')
subdir('src/parser_libs/audio_index_table')
subdir('src/parser_libs/aac_latm_parser')
subdir('src/parser_libs/h264_parser')
subdir('src/parser_libs/hevc_parser')


subdir('src/ape_parser')
subdir('src/avi_parser')
subdir('src/dsf_parser')
subdir('src/flv_parser')
subdir('src/mkv_parser')
#mp3 parser is not enabled in yocto, used in android
#subdir('src/mp3_parser')
subdir('src/mpg2_parser')
subdir('src/ogg_parser')
subdir('src/parser_test')
subdir('src/spdif_parser')
