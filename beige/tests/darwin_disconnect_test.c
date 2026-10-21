/* Exercise the real libusb Darwin backend with a removed device interface.
 * No USB device is opened; this reproduces the null pointer in the crash report.
 * Compile against the pinned dependency's private headers and static library. */
#include "config.h"
#include "libusbi.h"
#include "os/darwin_usb.h"
#include <assert.h>
#include <stdio.h>

static int refs;
static int submissions;
static IOReturn submit_result;
static struct darwin_cached_device *current;

static ULONG retain_device(void *device) { (void)device; return ++refs; }
static ULONG release_device(void *device) { (void)device; return --refs; }
static IOReturn submit_request(void *device, IOUSBDevRequestTO *request,
        IOAsyncCallback1 callback, void *context) {
    (void)device; (void)callback; (void)context;
    assert(refs == 2); // The submission owns a reference independent of hotplug.
    assert(request->wIndex == 0x110 && request->completionTimeout == 300);
    current->device = NULL; // Hotplug clears the cache during submission.
    ++submissions;
    return submit_result;
}

int main(void) {
    struct libusb_device *device = calloc(1, PTR_ALIGN(sizeof(*device)) + usbi_backend.device_priv_size);
    struct libusb_device_handle *handle = calloc(1, PTR_ALIGN(sizeof(*handle)) + usbi_backend.device_handle_priv_size);
    struct darwin_cached_device cached = {0};
    assert(device && handle);
    ((struct darwin_device_priv *)usbi_get_device_priv(device))->dev = &cached;
    handle->dev = device;
    unsigned char setup[LIBUSB_CONTROL_SETUP_SIZE];
    libusb_fill_control_setup(setup, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        0, 0x2000, 0x110, 0);
    struct libusb_transfer *transfer = libusb_alloc_transfer(0);
    assert(transfer);
    libusb_fill_control_transfer(transfer, handle, setup, NULL, NULL, 300);
    const int result = usbi_backend.submit_transfer(LIBUSB_TRANSFER_TO_USBI_TRANSFER(transfer));
    assert(result == LIBUSB_ERROR_NO_DEVICE);
    IOUSBDeviceInterface197 table = {0};
    table.AddRef = retain_device;
    table.Release = release_device;
    table.DeviceRequestAsyncTO = submit_request;
    IOUSBDeviceInterface197 *interface = &table;
    current = &cached;
    refs = 1;
    for (int i = 0; i < 1000; ++i) {
        cached.device = &interface;
        submit_result = i % 2 ? kIOReturnNoDevice : kIOReturnSuccess;
        const int expected = i % 2 ? LIBUSB_ERROR_NO_DEVICE : LIBUSB_SUCCESS;
        assert(usbi_backend.submit_transfer(LIBUSB_TRANSFER_TO_USBI_TRANSFER(transfer)) == expected);
        assert(refs == 1 && !cached.device);
    }
    assert(submissions == 1000);
    libusb_free_transfer(transfer);
    free(handle);
    free(device);
    puts("Darwin disconnect: null interface and 1000 submit/removal cases passed.");
    return 0;
}
