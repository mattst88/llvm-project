// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify %s

// A section of short data is writable whatever is in it, so an object that
// is constant and one that is not are both placed there, as GCC has it.
const int a __attribute__((section(".sdata"))) = 1;
int b __attribute__((section(".sdata"))) = 2;
const int c __attribute__((section(".sdata.x"))) = 1;
int d __attribute__((section(".sdata.x"))) = 2;
int e __attribute__((section(".sbss")));
const int f __attribute__((section(".sbss")));

// Any other section is of one kind.
const int g __attribute__((section(".mine"))) = 1; // expected-note {{declared here}}
int h __attribute__((section(".mine"))) = 2; // expected-error {{'h' causes a section type conflict with 'g'}}
