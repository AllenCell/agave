import struct

# command id will be int32 to future-proof it.
# note that the server needs to know these signatures too.
COMMANDS = {
    # tell server to identify this session?
    "SESSION": [0, "S"],
    # tell server where files might be (appends to existing)
    "ASSET_PATH": [1, "S"],
    # load a volume (DEPRECATED)
    "LOAD_OME_TIF": [2, "S"],
    # set camera pos
    "EYE": [3, "F32", "F32", "F32"],
    # set camera target pt
    "TARGET": [4, "F32", "F32", "F32"],
    # set camera up direction
    "UP": [5, "F32", "F32", "F32"],
    "APERTURE": [6, "F32"],
    # perspective(0)/ortho(1), fov(degrees)/orthoscale(world units)
    "CAMERA_PROJECTION": [7, "I32", "F32"],
    "FOCALDIST": [8, "F32"],
    "EXPOSURE": [9, "F32"],
    "MAT_DIFFUSE": [10, "I32", "F32", "F32", "F32", "F32"],
    "MAT_SPECULAR": [11, "I32", "F32", "F32", "F32", "F32"],
    "MAT_EMISSIVE": [12, "I32", "F32", "F32", "F32", "F32"],
    # set num render iterations
    "RENDER_ITERATIONS": [13, "I32"],
    # (continuous or on-demand frames)
    "STREAM_MODE": [14, "I32"],
    # request new image
    "REDRAW": [15],
    "SET_RESOLUTION": [16, "I32", "I32"],
    "DENSITY": [17, "F32"],
    # move camera to bound and look at the scene contents
    "FRAME_SCENE": [18],
    "MAT_GLOSSINESS": [19, "I32", "F32"],
    # channel index, 1/0 for enable/disable
    "ENABLE_CHANNEL": [20, "I32", "I32"],
    # channel index, window, level.  (Do I ever set these independently?)
    "SET_WINDOW_LEVEL": [21, "I32", "F32", "F32"],
    # theta, phi in degrees
    "ORBIT_CAMERA": [22, "F32", "F32"],
    "SKYLIGHT_TOP_COLOR": [23, "F32", "F32", "F32"],
    "SKYLIGHT_MIDDLE_COLOR": [24, "F32", "F32", "F32"],
    "SKYLIGHT_BOTTOM_COLOR": [25, "F32", "F32", "F32"],
    # r, theta, phi
    "LIGHT_POS": [26, "I32", "F32", "F32", "F32"],
    "LIGHT_COLOR": [27, "I32", "F32", "F32", "F32"],
    # x by y size
    "LIGHT_SIZE": [28, "I32", "F32", "F32"],
    # xmin, xmax, ymin, ymax, zmin, zmax
    "SET_CLIP_REGION": [29, "F32", "F32", "F32", "F32", "F32", "F32"],
    # x, y, z pixel scaling
    "SET_VOXEL_SCALE": [30, "F32", "F32", "F32"],
    # channel, method
    "AUTO_THRESHOLD": [31, "I32", "I32"],
    # channel index, pct_low, pct_high.  (Do I ever set these independently?)
    "SET_PERCENTILE_THRESHOLD": [32, "I32", "F32", "F32"],
    "MAT_OPACITY": [33, "I32", "F32"],
    "SET_PRIMARY_RAY_STEP_SIZE": [34, "F32"],
    "SET_SECONDARY_RAY_STEP_SIZE": [35, "F32"],
    # r, g, b
    "BACKGROUND_COLOR": [36, "F32", "F32", "F32"],
    # channel index, isovalue, isorange
    "SET_ISOVALUE_THRESHOLD": [37, "I32", "F32", "F32"],
    # channel index, array of [stop, r, g, b, a]
    "SET_CONTROL_POINTS": [38, "I32", "F32A"],
    # path, scene, time (DEPRECATED)
    "LOAD_VOLUME_FROM_FILE": [39, "S", "I32", "I32"],
    # causes data to be loaded
    "SET_TIME": [40, "I32"],
    "SET_BOUNDING_BOX_COLOR": [41, "F32", "F32", "F32"],
    "SHOW_BOUNDING_BOX": [42, "I32"],
    "TRACKBALL_CAMERA": [43, "F32", "F32"],
    # path, scene, multiresolution level, t, channel indices, region
    "LOAD_DATA": [44, "S", "I32", "I32", "I32", "I32A", "I32A"],
    "SHOW_SCALE_BAR": [45, "I32"],
    "SET_FLIP_AXIS": [46, "I32", "I32", "I32"],
    "SET_INTERPOLATION": [47, "I32"],
    "SET_CLIP_PLANE": [48, "F32", "F32", "F32", "F32"],
    # channel index, colormap name, array of [stop, r, g, b, a]
    "SET_COLOR_RAMP": [49, "I32", "S", "F32A"],
    "SET_MIN_MAX_THRESHOLD": [50, "I32", "I32", "I32"],
    # sphere (sky) light rotation as a quaternion (x, y, z, w)
    "SET_SKYLIGHT_ROTATION": [51, "F32", "F32", "F32", "F32"],
    "SHOW_TIME_STAMP": [52, "I32"],
    "SET_TIME_STAMP_FORMAT": [53, "I32"],
    "SET_MULTICHANNEL_BLEND": [54, "I32"],
}


# strategy: add elements to prebuffer,
# and then traverse prebuffer to convert to binary before sending?
class CommandBuffer:
    def __init__(self, command_list=None):
        # [command, args],...
        self.prebuffer = []
        self.buffer = None
        if command_list:
            for c in command_list:
                self.add_command(*c)

    def compute_size(self):
        # iterate length of prebuffer to compute size.
        bytesize = 0
        for command in self.prebuffer:
            # for each command.

            # one i32 for the command id.
            bytesize += 4

            commandCode = command[0]
            signature = COMMANDS[commandCode]
            nArgsExpected = len(signature) - 1
            # for each arg:
            if len(command) - 1 != nArgsExpected:
                print(
                    "BAD COMMAND: EXPECTED "
                    + str(nArgsExpected)
                    + " args and got "
                    + str(len(command) - 1)
                )
                return 0

            for j in range(0, nArgsExpected):
                # get arg type
                argtype = signature[j + 1]
                if argtype == "S":
                    # one int32 for string length
                    bytesize += 4
                    # followed by one byte per char.
                    bytesize += len(command[j + 1])
                elif argtype == "F32":
                    bytesize += 4
                elif argtype == "I32":
                    bytesize += 4
                elif argtype == "F32A":
                    # one int32 for array length
                    bytesize += 4
                    # followed by one float for each element in the array
                    bytesize += 4 * len(command[j + 1])
                elif argtype == "I32A":
                    # one int32 for array length
                    bytesize += 4
                    # followed by one int32 for each element in the array
                    bytesize += 4 * len(command[j + 1])
        return bytesize

    def make_buffer(self):
        bytesize = self.compute_size()

        # allocate arraybuffer and then fill it.
        self.buffer = bytearray(bytesize)

        offset = 0
        for cmd in self.prebuffer:
            commandCode = cmd[0]

            signature = COMMANDS.get(commandCode)
            if signature is None:
                raise KeyError(f"CommandBuffer: Unrecognized command {commandCode}")
            nArgsExpected = len(signature) - 1

            # the numeric code for the command
            struct.pack_into(">i", self.buffer, offset, signature[0])
            offset += 4
            for j in range(0, nArgsExpected):
                # get arg type
                argtype = signature[j + 1]
                if argtype == "S":
                    sstr = cmd[j + 1]
                    struct.pack_into(">i", self.buffer, offset, len(sstr))
                    offset += 4
                    for k in sstr:
                        struct.pack_into("B", self.buffer, offset, ord(k))
                        offset += 1
                elif argtype == "F32":
                    struct.pack_into("f", self.buffer, offset, cmd[j + 1])
                    offset += 4
                elif argtype == "I32":
                    struct.pack_into(">i", self.buffer, offset, cmd[j + 1])
                    offset += 4
                elif argtype == "F32A":
                    flist = cmd[j + 1]
                    struct.pack_into(">i", self.buffer, offset, len(flist))
                    offset += 4
                    for k in flist:
                        struct.pack_into("f", self.buffer, offset, k)
                        offset += 4
                elif argtype == "I32A":
                    ilist = cmd[j + 1]
                    struct.pack_into(">i", self.buffer, offset, len(ilist))
                    offset += 4
                    for k in ilist:
                        struct.pack_into(">i", self.buffer, offset, k)
                        offset += 4

        # result is in this.buffer
        return self.buffer

    # commands are added by command code string name
    # followed by appropriate signature args.
    def add_command(self, *args):
        # TODO: check against signature!!!
        self.prebuffer.append(args)


if __name__ == "__main__":
    cb = CommandBuffer()
    cb.add_command("EYE", 1.0, 1.0, 5.0)
    cb.add_command("TARGET", 3.0, 3.0, 0.0)
    cb.add_command("SESSION", "hello")
    cb.add_command("APERTURE", 7.0)
    buf = cb.make_buffer()
    print(list(buf))
