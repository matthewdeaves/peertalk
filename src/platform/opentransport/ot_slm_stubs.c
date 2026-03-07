/*
 * ot_slm_stubs.c -- Stub definitions for SLM dispatch symbols
 *
 * The 68k libOpenTransport.a references Shared Library Manager v1.1
 * dispatch routines (__SLM11VTableDispatch, etc.) that are resolved
 * at load time on real Macs but have no static implementation in
 * the Retro68 import libraries.
 *
 * These stubs satisfy the linker. On a real 68k Mac with OT installed,
 * the SLM resolves the actual implementations at load time via the
 * Code Fragment Manager (CFM68K).
 *
 * Only compiled for 68k OT builds (CMAKE_SYSTEM_NAME = Retro68,
 * PT_PLATFORM = OT).
 */

void __SLM11VTableDispatch(void) {}
void __SLM11ExtblDispatch(void) {}
void __SLM11ConstructorDispatch(void) {}
void __SLM11DestructorDispatch(void) {}

/* XTI/TLI compatibility symbols referenced by libOpenTransport.a */
int t_errno;
int errno;

/* C++ operator delete referenced by OT class destructors */
void __dl__FPv(void *p) { (void)p; }
