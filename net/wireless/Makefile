# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_CFG80211) += cfg80211.o
obj-$(CONFIG_LIB80211) += lib80211.o
obj-$(CONFIG_LIB80211_CRYPT_WEP) += lib80211_crypt_wep.o
obj-$(CONFIG_LIB80211_CRYPT_CCMP) += lib80211_crypt_ccmp.o
obj-$(CONFIG_LIB80211_CRYPT_TKIP) += lib80211_crypt_tkip.o

obj-$(CONFIG_WEXT_CORE) += wext-core.o
obj-$(CONFIG_WEXT_PROC) += wext-proc.o
obj-$(CONFIG_WEXT_SPY) += wext-spy.o
obj-$(CONFIG_WEXT_PRIV) += wext-priv.o

cfg80211-y += core.o sysfs.o radiotap.o util.o reg.o scan.o nl80211.o
cfg80211-y += mlme.o ibss.o sme.o chan.o ethtool.o mesh.o ap.o ocb.o
cfg80211-$(CONFIG_OF) += of.o
cfg80211-$(CONFIG_DEBUG_KERNEL) += trace.o
cfg80211-$(CONFIG_CFG80211_DEBUGFS) += debugfs.o
cfg80211-$(CONFIG_CFG80211_WEXT) += wext-compat.o wext-sme.o
cfg80211-$(CONFIG_CFG80211_INTERNAL_REGDB) += regdb.o

CFLAGS_trace.o := -I$(src)

clean-files += shipped-certs.c extra-certs.c
$(obj)/regdb.c: $(src)/db.txt $(src)/genregdb.awk
	@$(AWK) -f $(srctree)/$(src)/genregdb.awk < $< > $@

clean-files := regdb.c

cfg80211-$(CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS) += shipped-certs.o
ifneq ($(CONFIG_CFG80211_EXTRA_REGDB_KEYDIR),)
cfg80211-y += extra-certs.o
endif

$(obj)/shipped-certs.c: $(wildcard $(srctree)/$(src)/certs/*.hex)
	@$(kecho) "  GEN     $@"
	@(echo '#include "reg.h"'; \
	  echo 'const u8 shipped_regdb_certs[] = {'; \
	  echo | cat - $^ ; \
	  echo '};'; \
	  echo 'unsigned int shipped_regdb_certs_len = sizeof(shipped_regdb_certs);'; \
	 ) > $@

$(obj)/extra-certs.c: $(CONFIG_CFG80211_EXTRA_REGDB_KEYDIR:"%"=%) \
		      $(wildcard $(CONFIG_CFG80211_EXTRA_REGDB_KEYDIR:"%"=%)/*.x509)
	@$(kecho) "  GEN     $@"
	@(set -e; \
	  allf=""; \
	  for f in $^ ; do \
	      test -f $$f || continue;\
	      # similar to hexdump -v -e '1/1 "0x%.2x," "\n"' \
	      thisf=$$(od -An -v -tx1 < $$f | \
	                   sed -e 's/ /\n/g' | \
	                   sed -e 's/^[0-9a-f]\+$$/\0/;t;d' | \
	                   sed -e 's/^/0x/;s/$$/,/'); \
	      # file should not be empty - maybe command substitution failed? \
	      test ! -z "$$thisf";\
	      allf=$$allf$$thisf;\
	  done; \
	  ( \
	      echo '#include "reg.h"'; \
	      echo 'const u8 extra_regdb_certs[] = {'; \
	      echo "$$allf"; \
	      echo '};'; \
	      echo 'unsigned int extra_regdb_certs_len = sizeof(extra_regdb_certs);'; \
	  ) > $@)

