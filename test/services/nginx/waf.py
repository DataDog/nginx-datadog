import gdb


class PrintDDWAFObject(gdb.Command):

    def __init__(self):
        super(PrintDDWAFObject, self).__init__("print_ddwaf_object",
                                               gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        self.dont_repeat()
        obj = gdb.parse_and_eval(arg)
        if obj.type.code == gdb.TYPE_CODE_INT or obj.type.code == gdb.TYPE_CODE_PTR:
            obj = obj.cast(
                gdb.lookup_type('ddwaf_object').pointer()).dereference()
        self.print_object(obj, "")

    def print_object(self, obj, indent):
        obj_type = int(obj['type'])
        if obj_type == (1 << 0):  # DDWAF_OBJ_SIGNED
            print("%d" % obj['intValue'])
        elif obj_type == (1 << 1):  # DDWAF_OBJ_UNSIGNED
            print("%d" % obj['uintValue'])
        elif obj_type == (1 << 2):  # DDWAF_OBJ_STRING
            string_value = obj['stringValue']
            length = obj['nbEntries']
            print("\"%s\"" % self.read_memory_as_string(string_value, length))
        elif obj_type in [(1 << 3),
                          (1 << 4)]:  # DDWAF_OBJ_ARRAY or DDWAF_OBJ_MAP
            self.print_array_or_map(obj, indent, obj_type == (1 << 4))
        elif obj_type == (1 << 5):  # DDWAF_OBJ_BOOL
            print("%s" % obj['boolean'])
        elif obj_type == (1 << 6):  # DDWAF_OBJ_FLOAT
            print("%f" % obj['f64'])
        elif obj_type == (1 << 7):  # DDWAF_OBJ_NULL
            print("null")

    def print_array_or_map(self, obj, indent, is_map):
        array = obj['array']
        if obj['nbEntries'] == 0:
            if is_map:
                print('{}')
            else:
                print('[]')
            return

        print('')
        for i in range(obj['nbEntries']):
            if is_map:
                key_name = array[i]['parameterName']
                key_length = array[i]['parameterNameLength']
                key = self.read_memory_as_string(key_name, key_length)
                print(f"{indent}{key}: ", end="")
                self.print_object(array[i], indent + "  ")
            else:
                print(f"{indent}- ", end="")
                self.print_object(array[i], indent + "  ")

    def read_memory_as_string(self, address, length):
        memory = gdb.inferiors()[0].read_memory(address, length)
        return memory.tobytes().decode('utf-8')


PrintDDWAFObject()
