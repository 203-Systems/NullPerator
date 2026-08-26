#include "Adapters/node/filesystem/PathResolver.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {
class ScopedNodePathFixture {
public:
  ScopedNodePathFixture() {
    static unsigned sequence = 0;
    const std::string suffix = std::to_string(++sequence);
    root = std::filesystem::temp_directory_path() /
           ("picotracker-node-path-root-" + suffix);
    outside = std::filesystem::temp_directory_path() /
              ("picotracker-node-path-outside-" + suffix);
    std::filesystem::create_directories(root / "projects" / "safe");
    std::filesystem::create_directories(outside);
  }

  ~ScopedNodePathFixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::remove_all(outside, error);
  }

  std::filesystem::path root;
  std::filesystem::path outside;
};
} // namespace

TEST_CASE("Node path containment rejects an outside directory symlink") {
  ScopedNodePathFixture fixture;
  const auto link = fixture.root / "projects" / "outside-link";
  std::filesystem::create_directory_symlink(fixture.outside, link);
  const auto escapedExisting = link / "victim.dat";
  std::ofstream(fixture.outside / "victim.dat") << "outside";

  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), escapedExisting.string(), false));
  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), (link / "created.dat").string(), true));
  CHECK(std::filesystem::exists(fixture.outside / "victim.dat"));
}

TEST_CASE("Node path containment rejects a symlink leaf") {
  ScopedNodePathFixture fixture;
  const auto outsideFile = fixture.outside / "sample.wav";
  std::ofstream(outsideFile) << "sample";
  const auto link = fixture.root / "projects" / "sample.wav";
  std::filesystem::create_symlink(outsideFile, link);

  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), link.string(), false));
  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), link.string(), true));
  CHECK(std::filesystem::exists(outsideFile));
}

TEST_CASE("Node path containment accepts real paths and a missing suffix") {
  ScopedNodePathFixture fixture;
  const auto existing = fixture.root / "projects" / "safe";
  const auto missing = existing / "new" / "project.dat";

  CHECK(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), existing.string(), false));
  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), missing.string(), false));
  CHECK(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(), missing.string(), true));
  CHECK_FALSE(NodePath::IsContainedWithoutSymlinks(
      fixture.root.string(),
      (fixture.root.parent_path() / "outside.dat").string(), true));
}
