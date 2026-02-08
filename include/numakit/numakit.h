#ifndef LIBNUMAKIT_H
#define LIBNUMAKIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Versioning
#define NKIT_VERSION_MAJOR 0
#define NKIT_VERSION_MINOR 1
#define NKIT_VERSION_PATCH 0

/**
 * @brief Initialize the libnumakit library.
 * Detects topology and sets up internal structures.
 * Must be called before any other function.
 * * @return 0 on success, -1 on failure.
 */
int nkit_init(void);

/**
 * @brief Cleanup libnumakit resources.
 */
void nkit_teardown(void);

#ifdef __cplusplus
}
#endif

#endif // LIBNUMAKIT_H
