fractal-storage: fractal-storage.o mkfs/main.box.o $(filter-out mkfs/main.o,$(mkfs_objects)) $(objects) libbtrfsutil.a
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS) $(LIBS_COMP)
