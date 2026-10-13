import gdb

# libddwaf v2 object types
DDWAF_OBJ_INVALID = 0x00
DDWAF_OBJ_NULL = 0x01
DDWAF_OBJ_BOOL = 0x02
DDWAF_OBJ_SIGNED = 0x04
DDWAF_OBJ_UNSIGNED = 0x06
DDWAF_OBJ_FLOAT = 0x08
DDWAF_OBJ_STRING = 0x10
DDWAF_OBJ_LITERAL_STRING = 0x12
DDWAF_OBJ_SMALL_STRING = 0x14
DDWAF_OBJ_ARRAY = 0x20
DDWAF_OBJ_MAP = 0x40
DDWAF_OBJ_LARGE_ARRAY = 0xA0
DDWAF_OBJ_LARGE_MAP = 0xC0


class PrintDDWAFObject(gdb.Command):

    def __init__(self):
        super(PrintDDWAFObject, self).__init__("print_ddwaf_object",
                                               gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        self.dont_repeat()
        obj = gdb.parse_and_eval(arg)
        if obj.type.code in (gdb.TYPE_CODE_REF, gdb.TYPE_CODE_RVALUE_REF):
            obj = obj.referenced_value()
        if obj.type.code == gdb.TYPE_CODE_INT or obj.type.code == gdb.TYPE_CODE_PTR:
            obj = obj.cast(
                gdb.lookup_type('ddwaf_object').pointer()).dereference()
        # a ddwaf_obj (or one of its members) wraps a ddwaf_object at offset 0
        t = obj.type.strip_typedefs()
        if (t.code in (gdb.TYPE_CODE_UNION, gdb.TYPE_CODE_STRUCT)
                and t.tag != 'ddwaf_object'
                and not any(f.name == 'type' for f in t.fields())):
            obj = obj.address.cast(
                gdb.lookup_type('ddwaf_object').pointer()).dereference()
        self.print_object(obj, "")

    def print_object(self, obj, indent):
        obj_type = int(obj['type'])
        via = obj['via']
        if obj_type == DDWAF_OBJ_SIGNED:
            print("%d" % via['i64']['val'])
        elif obj_type == DDWAF_OBJ_UNSIGNED:
            print("%d" % via['u64']['val'])
        elif obj_type in (DDWAF_OBJ_STRING, DDWAF_OBJ_LITERAL_STRING,
                          DDWAF_OBJ_SMALL_STRING):
            print("\"%s\"" % self.read_string(obj))
        elif obj_type in (DDWAF_OBJ_ARRAY, DDWAF_OBJ_LARGE_ARRAY):
            self.print_container(obj, indent, False)
        elif obj_type in (DDWAF_OBJ_MAP, DDWAF_OBJ_LARGE_MAP):
            self.print_container(obj, indent, True)
        elif obj_type == DDWAF_OBJ_BOOL:
            print("%s" % via['b8']['val'])
        elif obj_type == DDWAF_OBJ_FLOAT:
            print("%f" % via['f64']['val'])
        elif obj_type == DDWAF_OBJ_NULL:
            print("null")
        else:
            print("<invalid>")

    def print_container(self, obj, indent, is_map):
        obj_type = int(obj['type'])
        member = {
            DDWAF_OBJ_ARRAY: 'array',
            DDWAF_OBJ_MAP: 'map',
            DDWAF_OBJ_LARGE_ARRAY: 'large_array',
            DDWAF_OBJ_LARGE_MAP: 'large_map',
        }[obj_type]
        container = obj['via'][member]
        size = int(container['size'])
        ptr = container['ptr']
        if size == 0:
            print('{}' if is_map else '[]')
            return

        print('')
        for i in range(size):
            if is_map:
                key = self.read_string(ptr[i]['key'])
                print(f"{indent}{key}: ", end="")
                self.print_object(ptr[i]['val'], indent + "  ")
            else:
                print(f"{indent}- ", end="")
                self.print_object(ptr[i], indent + "  ")

    def read_string(self, obj):
        obj_type = int(obj['type'])
        via = obj['via']
        if obj_type == DDWAF_OBJ_SMALL_STRING:
            address = via['sstr']['data'].address
            length = int(via['sstr']['size'])
        elif obj_type in (DDWAF_OBJ_STRING, DDWAF_OBJ_LITERAL_STRING):
            address = via['str']['ptr']
            length = int(via['str']['size'])
        else:
            return ""
        if length == 0:
            return ""
        memory = gdb.inferiors()[0].read_memory(address, length)
        return memory.tobytes().decode('utf-8', errors='replace')


PrintDDWAFObject()
