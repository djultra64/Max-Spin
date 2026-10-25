/* Full16:9 loading retains the original normalized Progress position/scale.
 * Preserve existing pipeline routing and original CPU effects. djultra64. */
extern void sub_0001D9F0_gen(void);
extern void __real_sub_000CB820(void);
void sub_0001D9F0(void)
{
 sub_0001D9F0_gen();
}
void __wrap_sub_000CB820(void)
{
 __real_sub_000CB820();
}
