#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include "xom.h"

jmp_buf fault_return;

// This is the function we want to move into XOM
unsigned int __attribute__((section(".data"))) secret_function (unsigned int plain_text) {
    return plain_text ^ 0xcafebabe;
}
void __attribute__((section(".data"))) secret_function_end (void) {}

void fault_handler (int signum) {
    sigset_t mask;
    puts("-> Trying to read XOM has caused a fault -> XOM works!");

    sigemptyset(&mask);
    sigaddset(&mask, signum);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    longjmp(fault_return, 1);
}

int main(int argc, char* argv[]) {
    // Get the function's size
    const size_t secret_function_size =
                (size_t) secret_function_end  -
                (size_t) secret_function;
    unsigned long secret_code;
    unsigned int (*secret_function_xom)(unsigned int);
    int status;

    // 'struct xombuf' is an anonymous struct representing one or more XOM pages
    struct xombuf* xbuf;

    unsigned int plain_text = 0xdeadbeef;
    unsigned int cipher_text;

    // Abort if XOM is not supported
    switch (get_xom_mode()) {
        case XOM_MODE_SLAT:
             puts("Using SLAT/EPT to enforce XOM!");
             break;
        case XOM_MODE_PKU:
             puts("Using MPK/PKU to enforce XOM!");
             break;
        case XOM_MODE_UNSUPPORTED:
        default:
            puts("XOM is not supported on your system!");
            return 1;
    }

    // Allocate a XOM buffer consisting of a single page
    xbuf = xom_alloc(PAGE_SIZE);
    if(!xbuf)
        return errno;

    // Write the secret function into the XOM buffer at offset 0
    status = xom_write(xbuf, secret_function, secret_function_size, 0);
    if(status <= 0)
        return errno;

    // Lock the XOM buffer, function returns a pointer to the XOM page itself
    secret_function_xom = xom_lock(xbuf);
    if(!secret_function_xom)
        return errno;

    // Overwrite the original function
    memset(secret_function, 0, secret_function_size);

    if(get_xom_mode() == XOM_MODE_SLAT) {
        // Mark the page for full register clearing if supported
        // The second parameter can alternatively be 0 for vector-register clearing
        status = xom_mark_register_clear(xbuf, 1, 0);
        if (status < 0)
            return -status;
    }

    puts("-> Successfully set up an XOM range");

    // Call the function in XOM
    // The following block restarts when full register clearing occurs
    expect_full_register_clear {
        cipher_text = secret_function_xom(plain_text);
    }

    if (cipher_text != (plain_text ^ 0xcafebabe))
        puts("Error: Wrong output!");

    puts("-> XOM range can be executed!");

    signal(SIGSEGV, fault_handler);
    if (!setjmp(fault_return)) {
        // Try to read from XOM
        secret_code = *(unsigned long*) secret_function_xom;

        // If XOM works, we will not end up here, and instead enter the fault handler
        printf("We could read the value %lx from XOM. This is bad.\n", secret_code);
    }

    // Free the XOM buffer
    xom_free(xbuf);

    return 0;
}

