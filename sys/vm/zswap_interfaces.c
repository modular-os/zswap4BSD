// This file define those interfaces for migrating zswap from Linux
// writed by modular-os-group.

// This file include those interfaces:
// Compress Module For Zswap (Write: Fan Yi)
// Invoker Interface for FreeBSD (Write: Yi Ran)
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/string.h>
#include "zswap_interfaces.h"

int param_get_bool(char *buffer, const struct kernel_param *kp) {
    return 0;
}




/* crypto */
void crypto_req_done(void *data, int err)
{
    return;
}
EXPORT_SYMBOL_GPL(crypto_req_done);


// string.c

/**
 * strim - Removes leading and trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns a pointer to the first non-whitespace
 * character in @s.
 */
char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}

int param_set_charp(const char *val, const struct kernel_param *kp)
{
	return 0;
}
EXPORT_SYMBOL(param_set_charp);

int param_set_bool(const char *val, const struct kernel_param *kp)
{
    return 0;
	// /* No equals means "set"... */
	// if (!val) val = "1";

	// /* One of =[yYnN01] */
	// return kstrtobool(val, kp->arg);
}
EXPORT_SYMBOL(param_set_bool);

enum system_states system_state __read_mostly;