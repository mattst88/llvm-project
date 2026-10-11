// RUN: %clang_cc1 -triple ia64-unknown-linux-gnu -fsyntax-only -verify %s

__attribute__((model(small))) long a;
__attribute__((model(__small__))) long b;
__attribute__((model(large))) long c; // expected-warning {{'model' attribute argument not supported: 'large'}}
__attribute__((model("small"))) long d; // expected-error {{'model' attribute requires an identifier}}
__attribute__((model)) long e; // expected-error {{'model' attribute takes one argument}}
__attribute__((model(small))) void f(void); // expected-error {{'model' attribute only applies to non-TLS global variables}}
__attribute__((model(small))) __thread long g; // expected-error {{'model' attribute only applies to non-TLS global variables}}
