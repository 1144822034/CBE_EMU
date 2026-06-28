mergeInto(LibraryManager.library, {
  saveSetjmp__sig: "iiiii",
  saveSetjmp: function(env, label, table, size) {
    env = env | 0;
    label = label | 0;
    table = table | 0;
    size = size | 0;

    for (var i = 0; i < size; i++) {
      var slot = HEAP32[(table + (i << 2)) >> 2] | 0;
      if (slot === 0 || slot === env) {
        HEAP32[(table + (i << 2)) >> 2] = env;
        HEAP32[(table + ((i + size) << 2)) >> 2] = label;
        return label | 0;
      }
    }

    throw new Error("legacy saveSetjmp table is full");
  },

  testSetjmp__sig: "iiii",
  testSetjmp: function(id, table, size) {
    id = id | 0;
    table = table | 0;
    size = size | 0;

    for (var i = 0; i < size; i++) {
      if ((HEAP32[(table + (i << 2)) >> 2] | 0) === id) {
        return HEAP32[(table + ((i + size) << 2)) >> 2] | 0;
      }
    }

    return 0;
  }
});
