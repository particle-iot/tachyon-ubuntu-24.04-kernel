metapkgs := linux-image-particle linux-headers-particle linux-particle linux-tools-particle

.PHONY: meta-particle
meta-particle: debian/control
	@set -e; for p in $(metapkgs); do \
	  if grep -q "^Package: $$p$$" debian/control; then \
	    mkdir -p debian/$$p || true; \
	    echo "==> Packaging $$p"; \
	    dh_prep -p$$p; \
	    dh_installdocs -p$$p || true; \
	    dh_compress -p$$p; \
	    dh_fixperms -p$$p; \
	    dh_installdeb -p$$p; \
	    dh_gencontrol -p$$p; \
	    dh_md5sums -p$$p; \
	    dh_builddeb -p$$p -- -Znone; \
	  fi; \
	done
