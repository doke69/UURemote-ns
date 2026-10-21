// Deliberately no mutation backend. A client-side check/set/restore cannot
// coordinate the system-wide multicast list. Do not add an enable switch.
// See DESIGN.md for the service-side ownership requirements.
#ifndef UU_IPV6_READONLY_MEMBERSHIP_H
#define UU_IPV6_READONLY_MEMBERSHIP_H
#define UU_IPV6_MEMBERSHIP_READONLY 1
#endif
