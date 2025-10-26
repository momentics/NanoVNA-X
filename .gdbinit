set pagination off

# Connect to the OpenOCD GDB server (default configuration).
target extended-remote :4242
monitor reset halt

# Stop as soon as the firmware raises a hard fault to inspect the captured state.
break hard_fault_handler_c
