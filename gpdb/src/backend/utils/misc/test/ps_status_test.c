#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "cmockery.h"

#include "../ps_status.c"

/*
 * Check it won't crash in case the ps_buffer overflows.
 */
void
test__set_ps_display(void **state)
{
	ps_buffer = (char *) malloc(64 * sizeof(char));
	memset(ps_buffer, 0x7F, 64 * sizeof(char));
	ps_buffer_fixed_size = 25;
	ps_buffer_size = 32;
	IsUnderPostmaster = true;

	gp_session_id = 1024;
	Gp_role = GP_ROLE_DISPATCH;
	Gp_segment = 24;
	gp_command_count = 1024;
	currentSliceId = 40;

	set_ps_display("testing activity", true);
	set_ps_display("testing activity", false);

	assert_true(ps_buffer[32] == 0x7f);
}

/*
 * MPP-220077: real_act_prefix_size should not go beyond ps_buffer_size
 */
void
test__set_ps_display__real_act_prefix_size(void **state)
{
	int		len;

	ps_buffer = (char *) malloc(127 * sizeof(char));
	ps_buffer_fixed_size = 79;
	memset(ps_buffer, 'x', ps_buffer_fixed_size * sizeof(char));
	ps_buffer_size = 127;
	IsUnderPostmaster = true;

	StrNCpy(ps_host_info, "msa4000125.europe.corp.microsoft.com(57193)",
			sizeof(ps_host_info));
	ps_host_info_size = 0;
	gp_session_id = 26351;
	Gp_role = GP_ROLE_DISPATCH;
	Gp_segment = -1;
	gp_command_count = 964;
	currentSliceId = -1;

	set_ps_display("testing activity", true);
	assert_true(real_act_prefix_size <= ps_buffer_size);

	get_real_act_ps_display(&len);
	assert_true(len >= 0);
}

int
main(int argc, char* argv[])
{
	cmockery_parse_arguments(argc, argv);

	const UnitTest tests[] = {
			unit_test(test__set_ps_display),
			unit_test(test__set_ps_display__real_act_prefix_size)
	};
	return run_tests(tests);
}
