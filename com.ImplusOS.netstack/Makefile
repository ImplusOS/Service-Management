.PHONY: all clean

include ../Source/Common.mk

SERVICE_SRCS := $(wildcard DNS/*.c)
SERVICE_OBJS := $(SERVICE_SRCS:%.c=$(SERVICE_BUILD)/%.o)

all: $(SERVICE_SO)

$(SERVICE_SO): $(SERVICE_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(SERVICE_LDFLAGS) --unresolved-symbols=ignore-all $^ -o $@

clean:
	rm -rf $(SERVICE_BUILD)

-include $(SERVICE_OBJS:.o=.d)
