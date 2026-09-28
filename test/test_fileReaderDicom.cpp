#include "renderlib/ImageXYZC.h"
#include "renderlib/io/FileReader.h"
#include "renderlib/io/FileReaderDicom.h"
#include "renderlib/io/LoadRequest.h"

#include <catch2/catch_test_macros.hpp>

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>

#include <chrono>
#include <filesystem>
#include <memory>

namespace {
void
writeSlice(const std::filesystem::path& path, const char* seriesUid, const char* instanceUid, int z, Uint16 base)
{
  DcmFileFormat file;
  DcmDataset* dataset = file.getDataset();
  REQUIRE(dataset->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage).good());
  REQUIRE(dataset->putAndInsertString(DCM_SOPInstanceUID, instanceUid).good());
  REQUIRE(dataset->putAndInsertString(DCM_SeriesInstanceUID, seriesUid).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_Rows, 2).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_Columns, 2).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_SamplesPerPixel, 1).good());
  REQUIRE(dataset->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2").good());
  REQUIRE(dataset->putAndInsertUint16(DCM_BitsAllocated, 16).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_BitsStored, 16).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_HighBit, 15).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_PixelRepresentation, 0).good());
  REQUIRE(dataset->putAndInsertString(DCM_PixelSpacing, "0.5\\0.25").good());
  REQUIRE(dataset->putAndInsertString(DCM_ImageOrientationPatient, "1\\0\\0\\0\\1\\0").good());
  const std::string position = "0\\0\\" + std::to_string(z);
  REQUIRE(dataset->putAndInsertString(DCM_ImagePositionPatient, position.c_str()).good());
  const std::string instance = std::to_string(20 - z);
  REQUIRE(dataset->putAndInsertString(DCM_InstanceNumber, instance.c_str()).good());
  const Uint16 pixels[] = {
    base, static_cast<Uint16>(base + 1), static_cast<Uint16>(base + 2), static_cast<Uint16>(base + 3)
  };
  REQUIRE(dataset->putAndInsertUint16Array(DCM_PixelData, pixels, 4).good());
  REQUIRE(file.saveFile(path.string().c_str(), EXS_LittleEndianExplicit).good());
}
} // namespace

TEST_CASE("DICOM reader loads a multiframe grayscale volume")
{
  const auto path =
    std::filesystem::temp_directory_path() /
    ("agave-dicom-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dcm");
  DcmFileFormat file;
  DcmDataset* dataset = file.getDataset();
  REQUIRE(dataset->putAndInsertString(DCM_SOPClassUID, UID_SecondaryCaptureImageStorage).good());
  REQUIRE(dataset->putAndInsertString(DCM_SOPInstanceUID, "1.2.826.0.1.3680043.2.1125.1").good());
  REQUIRE(dataset->putAndInsertUint16(DCM_Rows, 2).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_Columns, 2).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_SamplesPerPixel, 1).good());
  REQUIRE(dataset->putAndInsertString(DCM_PhotometricInterpretation, "MONOCHROME2").good());
  REQUIRE(dataset->putAndInsertUint16(DCM_BitsAllocated, 16).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_BitsStored, 16).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_HighBit, 15).good());
  REQUIRE(dataset->putAndInsertUint16(DCM_PixelRepresentation, 0).good());
  REQUIRE(dataset->putAndInsertString(DCM_NumberOfFrames, "2").good());
  REQUIRE(dataset->putAndInsertString(DCM_PixelSpacing, "0.5\\0.25").good());
  REQUIRE(dataset->putAndInsertString(DCM_SpacingBetweenSlices, "1.5").good());
  const Uint16 pixels[] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 65535 };
  REQUIRE(dataset->putAndInsertUint16Array(DCM_PixelData, pixels, 8).good());
  REQUIRE(file.saveFile(path.string().c_str(), EXS_LittleEndianExplicit).good());

  std::unique_ptr<IFileReader> reader(FileReader::getReader(path.string()));
  REQUIRE(dynamic_cast<FileReaderDicom*>(reader.get()) != nullptr);
  CHECK(reader->loadNumScenes(path.string()) == 1);
  const auto dims = reader->loadDimensions(path.string());
  CHECK(dims.sizeX == 2);
  CHECK(dims.sizeY == 2);
  CHECK(dims.sizeZ == 2);
  CHECK(dims.sizeC == 1);
  CHECK(dims.physicalSizeX == 0.25f);
  CHECK(dims.physicalSizeY == 0.5f);
  CHECK(dims.physicalSizeZ == 1.5f);
  CHECK(dims.spatialUnits == "mm");
  const auto scales = reader->loadMultiscaleDims(path.string());
  REQUIRE(scales.size() == 1);
  CHECK(scales[0].shape == std::vector<int64_t>{ 1, 1, 2, 2, 2 });

  LoadSpec spec;
  spec.filepath = path.string();
  LoadProgress progress;
  auto image = dynamic_cast<FileReaderDicom*>(reader.get())->loadVolumeBlocking(spec, progress);
  REQUIRE(image != nullptr);
  CHECK(image->sizeZ() == 2);
  CHECK(image->sizeC() == 1);
  CHECK(image->physicalSizeX() == 0.25f);
  const auto* first = reinterpret_cast<const uint16_t*>(image->ptr(0, 0));
  const auto* second = reinterpret_cast<const uint16_t*>(image->ptr(0, 1));
  CHECK(first[0] < first[3]);
  CHECK(first[3] < second[3]);
  CHECK(progress.progress() == 1.0f);
  std::filesystem::remove(path);
}

TEST_CASE("DICOM folder groups series and orders slices by position")
{
  const auto root =
    std::filesystem::temp_directory_path() /
    ("agave-dicom-folder-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "nested");
  // Filenames and instance numbers run opposite to the spatial order.
  writeSlice(root / "a.dcm", "1.2.826.0.1.3680043.2.1125.100", "1.2.826.0.1.3680043.2.1125.101", 2, 3000);
  writeSlice(root / "nested" / "slice0", "1.2.826.0.1.3680043.2.1125.100", "1.2.826.0.1.3680043.2.1125.102", 0, 1000);
  writeSlice(root / "c.dcm", "1.2.826.0.1.3680043.2.1125.100", "1.2.826.0.1.3680043.2.1125.103", 1, 2000);
  writeSlice(
    root / "nested" / "other.dcm", "1.2.826.0.1.3680043.2.1125.200", "1.2.826.0.1.3680043.2.1125.201", 0, 4000);

  std::unique_ptr<IFileReader> reader(FileReader::getReader(root.string()));
  REQUIRE(dynamic_cast<FileReaderDicom*>(reader.get()) != nullptr);
  REQUIRE(reader->loadNumScenes(root.string()) == 2);
  const auto dims = reader->loadDimensions(root.string(), 0);
  CHECK(dims.sizeZ == 3);
  CHECK(dims.physicalSizeZ == 1.0f);
  CHECK(reader->loadDimensions(root.string(), 1).sizeZ == 1);
  LoadSpec spec;
  spec.filepath = root.string();
  LoadProgress progress;
  auto image = dynamic_cast<FileReaderDicom*>(reader.get())->loadVolumeBlocking(spec, progress);
  REQUIRE(image != nullptr);
  REQUIRE(image->sizeZ() == 3);
  const auto first = reinterpret_cast<const uint16_t*>(image->ptr(0, 0))[0];
  const auto second = reinterpret_cast<const uint16_t*>(image->ptr(0, 1))[0];
  const auto third = reinterpret_cast<const uint16_t*>(image->ptr(0, 2))[0];
  CHECK(first < second);
  CHECK(second < third);
  CHECK(progress.progress() == 1.0f);
  std::filesystem::remove_all(root);
}
