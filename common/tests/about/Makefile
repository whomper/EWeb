# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/aboutline.c (the About box's status lines) and ui/aboutpen.h (its icon's nearest pen) on the host: make test
B := build
$(B)/test_aboutline: test_aboutline.c ../../ui/aboutline.c ../../ui/aboutline.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -DABOUTLINE_HOST -I../../ui -o $@ test_aboutline.c ../../ui/aboutline.c
$(B)/test_aboutpen: test_aboutpen.c ../../ui/aboutpen.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -I../../ui -o $@ test_aboutpen.c
test: $(B)/test_aboutline $(B)/test_aboutpen
	./$(B)/test_aboutline
	./$(B)/test_aboutpen
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: test clean
