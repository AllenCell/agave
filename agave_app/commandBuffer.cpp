#include "commandBuffer.h"

#include "command.h"
#include "renderlib/Logging.h"

#include <algorithm>
#include <assert.h>
#include <vector>

#if HAVE_BYTESWAP_H
#include <byteswap.h>
#else
#define bswap_16(value) ((((value) & 0xff) << 8) | ((value) >> 8))

#define bswap_32(value)                                                                                                \
  (((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | (uint32_t)bswap_16((uint16_t)((value) >> 16)))

#define bswap_64(value)                                                                                                \
  (((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) << 32) | (uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif

commandBuffer::commandBuffer(size_t len, const uint8_t* buf)
  : _length(len)
  , _headPos(buf)
{
}

commandBuffer::~commandBuffer() {}

CommandBufferIterator::CommandBufferIterator(commandBuffer* buf)
  : _commandBuffer(buf)
  , _currentPos(const_cast<uint8_t*>(buf->head()))
{
}

//////////////////
// forward declare.

#define CMD_CASE(CMDCLASS)                                                                                             \
  case (CMDCLASS::m_ID):                                                                                               \
    return CMDCLASS::parse(&iterator);                                                                                 \
    break;

commandBuffer*
commandBuffer::createBuffer(const std::vector<Command*>& commands)
{
  // compute space for allocation
  size_t len = 0;
  CommandBufferSizer sizer;
  for (auto command : commands) {
    len += command->write(&sizer);
  }

  uint8_t* buf = new uint8_t[len];
  commandBuffer* cb = new commandBuffer(len, buf);
  CommandBufferWriter cbw(cb);
  // write into buf
  size_t bytesWritten = 0;
  for (auto command : commands) {
    bytesWritten += command->write(&cbw);
  }
  // TODO throw on failure???
  assert(bytesWritten == len);
  return cb;
}

void
commandBuffer::processBuffer()
{
  int32_t previousCmd = -1;
  CommandBufferIterator iterator(this);
  while (!iterator.end()) {
    // new command.
    // read its int32 enum value.
    int32_t cmd = iterator.parseInt32();

    // lambda that takes our iterator and the cmd id to initialize the command object.
    Command* c = [cmd, &iterator]() -> Command* {
      try {
        switch (cmd) {
          CMD_CASE(SessionCommand);
          CMD_CASE(AssetPathCommand);
          CMD_CASE(LoadOmeTifCommand);
          CMD_CASE(SetCameraPosCommand);
          CMD_CASE(SetCameraTargetCommand);
          CMD_CASE(SetCameraUpCommand);
          CMD_CASE(SetCameraApertureCommand);
          CMD_CASE(SetCameraProjectionCommand);
          CMD_CASE(SetCameraFocalDistanceCommand);
          CMD_CASE(SetCameraExposureCommand);
          CMD_CASE(SetDiffuseColorCommand);
          CMD_CASE(SetSpecularColorCommand);
          CMD_CASE(SetEmissiveColorCommand);
          CMD_CASE(SetRenderIterationsCommand);
          CMD_CASE(SetStreamModeCommand);
          CMD_CASE(RequestRedrawCommand);
          CMD_CASE(SetResolutionCommand);
          CMD_CASE(SetDensityCommand);
          CMD_CASE(FrameSceneCommand);
          CMD_CASE(SetGlossinessCommand);
          CMD_CASE(EnableChannelCommand);
          CMD_CASE(SetWindowLevelCommand);
          CMD_CASE(OrbitCameraCommand);
          CMD_CASE(SetSkylightTopColorCommand);
          CMD_CASE(SetSkylightMiddleColorCommand);
          CMD_CASE(SetSkylightBottomColorCommand);
          CMD_CASE(SetLightPosCommand);
          CMD_CASE(SetLightColorCommand);
          CMD_CASE(SetLightSizeCommand);
          CMD_CASE(SetClipRegionCommand);
          CMD_CASE(SetVoxelScaleCommand);
          CMD_CASE(AutoThresholdCommand);
          CMD_CASE(SetPercentileThresholdCommand);
          CMD_CASE(SetOpacityCommand);
          CMD_CASE(SetPrimaryRayStepSizeCommand);
          CMD_CASE(SetSecondaryRayStepSizeCommand);
          CMD_CASE(SetBackgroundColorCommand);
          CMD_CASE(SetIsovalueThresholdCommand);
          CMD_CASE(SetControlPointsCommand);
          CMD_CASE(LoadVolumeFromFileCommand);
          CMD_CASE(SetTimeCommand);
          CMD_CASE(SetBoundingBoxColorCommand);
          CMD_CASE(ShowBoundingBoxCommand);
          CMD_CASE(TrackballCameraCommand);
          CMD_CASE(LoadDataCommand);
          CMD_CASE(ShowScaleBarCommand);
          CMD_CASE(SetFlipAxisCommand);
          CMD_CASE(SetInterpolationCommand);
          CMD_CASE(SetClipPlaneCommand);
          CMD_CASE(SetColorRampCommand);
          CMD_CASE(SetMinMaxThresholdCommand);
          CMD_CASE(SetSkylightRotationCommand);
          CMD_CASE(ShowTimeStampCommand);
          CMD_CASE(SetTimeStampFormatCommand);
          CMD_CASE(SetMultichannelBlendCommand);
          default:
            // ERROR UNRECOGNIZED COMMAND SIGNATURE.
            // PRINT OUT PREVIOUS! BAIL OUT! OR DO SOMETHING CLEVER AND CORRECT!
            LOG_WARNING << "Unrecognized command index: " << cmd;
            return nullptr;
            break;
        }
      } catch (...) {
        // buffer error?
        LOG_WARNING << "Exception thrown when parsing command index: " << cmd;
        return nullptr;
      }
      // we should never get here
      return nullptr;
    }();

    if (c) {
      // good! add to list of commands
      _commands.push_back(c);
    } else {
      // error! do something.
      LOG_WARNING << "Previous parsed command :" << previousCmd;
      LOG_WARNING << "No further commands will be parsed for this batch.";
      break;
    }
    previousCmd = cmd;
  }
}

void
commandBuffer::execute(ExecutionContext* c)
{
  // let's run all the commands now
  for (auto i = _commands.begin(); i != _commands.end(); ++i) {
    (*i)->execute(c);
  }
}

bool
CommandBufferIterator::end()
{
  return (_currentPos >= _commandBuffer->head() + _commandBuffer->length());
}

int32_t
CommandBufferIterator::parseInt32()
{
  int32_t value = bswap_32(*((int32_t*)(_currentPos)));
  _currentPos += sizeof(int32_t);
  return value;
}

float
CommandBufferIterator::parseFloat32()
{
  // assuming sizeof float == sizeof int32 == 4
  // float value = (float)bswap_32(*((int32_t*)(_currentPos)));
  float value = *((float*)(_currentPos));
  _currentPos += sizeof(float);
  return value;
}

std::string
CommandBufferIterator::parseString()
{
  int32_t len = parseInt32();
  std::string s(reinterpret_cast<char const*>(_currentPos), (size_t)len);
  _currentPos += len;
  return s;
}

std::vector<float>
CommandBufferIterator::parseFloat32Array()
{
  int32_t len = parseInt32();
  float* p = (float*)(_currentPos);
  std::vector<float> v(p, p + len);
  _currentPos += len * sizeof(float);
  return v;
}

std::vector<int32_t>
CommandBufferIterator::parseInt32Array()
{
  int32_t len = parseInt32();
  std::vector<int32_t> v(len);
  for (int i = 0; i < len; ++i) {
    int32_t value = parseInt32();
    v[i] = value;
  }
  return v;
}

CommandBufferWriter::CommandBufferWriter(commandBuffer* cb)
  : _commandBuffer(cb)
  , _currentPos(const_cast<uint8_t*>(cb->head()))
{
}

size_t
CommandBufferWriter::writeInt32(int32_t i)
{
  int32_t* p = (int32_t*)(_currentPos);
  *p = bswap_32(i);
  assert(sizeof(int32_t) == 4);
  _currentPos += 4; // sizeof(int32_t);
  return 4;
}

size_t
CommandBufferWriter::writeFloat32(float f)
{
  float* p = (float*)(_currentPos);
  *p = f;
  assert(sizeof(float) == 4);
  _currentPos += 4; // sizeof(float);
  return 4;
}

size_t
CommandBufferWriter::writeFloat32Array(const std::vector<float>& v)
{
  writeInt32((int32_t)v.size());
  memcpy(_currentPos, v.data(), v.size() * sizeof(float));
  _currentPos += v.size() * sizeof(float);
  return 4 + v.size() * sizeof(float);
}

size_t
CommandBufferWriter::writeInt32Array(const std::vector<int32_t>& v)
{
  writeInt32((int32_t)v.size());
  for (auto i = v.begin(); i != v.end(); ++i) {
    writeInt32(*i);
  }
  return 4 + v.size() * sizeof(int32_t);
}

size_t
CommandBufferWriter::writeString(const std::string& s)
{
  writeInt32((int32_t)s.size());
  memcpy(_currentPos, s.data(), s.size() * sizeof(uint8_t));
  _currentPos += s.size() * sizeof(uint8_t);
  return 4 + s.size() * sizeof(uint8_t);
}

size_t
CommandBufferSizer::writeInt32(int32_t i)
{
  return 4; // sizeof(int32_t);
}

size_t
CommandBufferSizer::writeFloat32(float f)
{
  return 4; // sizeof(float);
}

size_t
CommandBufferSizer::writeFloat32Array(const std::vector<float>& v)
{
  return 4 + v.size() * sizeof(float);
}

size_t
CommandBufferSizer::writeInt32Array(const std::vector<int32_t>& v)
{
  return 4 + v.size() * sizeof(int32_t);
}

size_t
CommandBufferSizer::writeString(const std::string& s)
{
  return 4 + s.size() * sizeof(uint8_t);
}
