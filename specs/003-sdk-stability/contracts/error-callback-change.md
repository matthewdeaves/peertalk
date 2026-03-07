# API Contract Change: Error Callback Signature

**Type**: Breaking change
**Affected**: `PT_ErrorCallback` typedef in `peertalk.h`

## Before

```c
typedef void (*PT_ErrorCallback)(
    PT_Status error,
    const char *description,
    void *user_data
);
```

## After

```c
typedef void (*PT_ErrorCallback)(
    PT_Peer *peer,          /* NULL for non-peer errors */
    PT_Status error,
    const char *description,
    void *user_data
);
```

## Migration

All error callback implementations must add `PT_Peer *peer` as first parameter:

```c
/* Before */
static void on_error(PT_Status error, const char *desc, void *data)

/* After */
static void on_error(PT_Peer *peer, PT_Status error, const char *desc, void *data)
```

## Peer Context by Error Type

| Error | Peer parameter |
|-------|---------------|
| Connection timeout | Non-NULL (the peer that timed out) |
| Send failure | Non-NULL (the peer send failed to) |
| No peer slots | NULL |
| Reassembly overflow | Non-NULL (the sending peer) |
| Init/discovery failures | NULL |
