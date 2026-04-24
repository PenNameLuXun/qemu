jxl_ss = ss.source_set()
jxl_ss.add(when: 'CONFIG_JXL', if_true: files('jxl_board.c', 'jxl_soc.c'))

arm_common_ss.add_all(jxl_ss)
