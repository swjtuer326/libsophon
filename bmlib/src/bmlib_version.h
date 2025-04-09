#ifndef _BMLIB_VERSION_H_
#define _BMLIB_VERSION_H_

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#endif
#define COMMIT_HASH "a4962999ee25e2da65eba2a846466e1be805406a"
#define BRANCH_NAME "v23.09-LTS"

#endif