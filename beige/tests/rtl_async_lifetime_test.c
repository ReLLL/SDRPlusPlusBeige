/* Exercise the pinned production driver with delayed USB completions. No radio.
 * The source is included to construct its private device without opening USB. */
#include <assert.h>
#include <stdio.h>
#include RTLSDR_SOURCE_FILE

static struct libusb_transfer *slots[8];
static int pending[8], cancelling[8], ticks, submitted, freed, fault, scenario;
static rtlsdr_dev_t *current_dev;

static int index_of(struct libusb_transfer *transfer) {
    for (int i = 0; i < 8; ++i) if (slots[i] == transfer) return i;
    assert(!"unknown transfer"); return -1;
}

struct libusb_transfer *LIBUSB_CALL libusb_alloc_transfer(int packets) {
    assert(packets == 0);
    for (int i = 0; i < 8; ++i) if (!slots[i]) {
        slots[i] = calloc(1, sizeof(struct libusb_transfer));
        assert(slots[i]); return slots[i];
    }
    assert(!"too many transfers"); return NULL;
}

void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *transfer) {
    int i = index_of(transfer);
    if (pending[i]) fprintf(stderr, "REGRESSION: freeing submitted USB transfer before its completion\n");
    assert(!pending[i]);
    free(transfer); slots[i] = NULL; ++freed;
}

int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *transfer) {
    int i = index_of(transfer);
    if (scenario == 2 && submitted == 1 && !fault) { fault = 1; return LIBUSB_ERROR_IO; }
    if (scenario == 4 && submitted >= 4 && !fault) { fault = 1; return LIBUSB_ERROR_IO; }
    assert(!pending[i]); pending[i] = 1; cancelling[i] = 0; ++submitted;
    return 0;
}

int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *transfer) {
    int i = index_of(transfer);
    if (!pending[i]) return LIBUSB_ERROR_NOT_FOUND;
    cancelling[i] = 1;
    return 0;
}

int LIBUSB_CALL libusb_handle_events_timeout_completed(libusb_context *ctx,
        struct timeval *timeout, int *completed) {
    (void)ctx; (void)timeout; (void)completed;
    assert(++ticks < 5000);
    if (scenario == 0 && !fault) { fault = 1; return LIBUSB_ERROR_IO; }
    if (scenario == 5 && ticks < 30) return LIBUSB_ERROR_IO;
    if (scenario == 6 && ticks == 1) return LIBUSB_ERROR_INTERRUPTED;
    if ((scenario == 3 || scenario == 6) && ticks == 2) rtlsdr_cancel_async(current_dev);
    for (int i = 0; i < 8; ++i) if (slots[i] && pending[i]) {
        if (scenario == 1 && !fault) {
            fault = 1; pending[i] = 0;
            slots[i]->status = LIBUSB_TRANSFER_NO_DEVICE;
            slots[i]->callback(slots[i]); return 0;
        }
        if (scenario == 4 && !fault) {
            pending[i] = 0;
            slots[i]->status = LIBUSB_TRANSFER_COMPLETED;
            slots[i]->callback(slots[i]); return 0;
        }
        if (cancelling[i] && ticks > 40) {
            pending[i] = 0;
            slots[i]->status = LIBUSB_TRANSFER_CANCELLED;
            slots[i]->callback(slots[i]);
            return 0; // Deliberately complete only one transfer per event call.
        }
    }
    return 0;
}

static void run_case(void) {
    ticks = submitted = freed = fault = 0;
    memset(pending, 0, sizeof(pending));
    memset(cancelling, 0, sizeof(cancelling));
    rtlsdr_dev_t device = {0};
    current_dev = &device;
    device.devh = (libusb_device_handle *)&device;
    const int result = rtlsdr_read_async(&device, NULL, NULL, 4, 512);
    assert(device.async_status == RTLSDR_INACTIVE && !device.xfer && !device.xfer_buf);
    for (int i = 0; i < 8; ++i) assert(!pending[i] && !slots[i]);
    assert(freed == 4);
    if (scenario == 3 || scenario == 6) assert(result == 0);
    else assert(result < 0);
}

int main(int argc, char **argv) {
    const int first = argc > 1 ? atoi(argv[1]) : 0;
    const int last = argc > 1 ? first : 6;
    for (scenario = first; scenario <= last; ++scenario) {
        for (int repeat = 0; repeat < 100; ++repeat) run_case();
        printf("RTL async lifetime scenario %d: 100 delayed-completion cycles passed.\n", scenario);
    }
}
