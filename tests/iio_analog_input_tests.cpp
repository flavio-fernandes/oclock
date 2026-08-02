#include "adc/AnalogInput.h"
#include "adc/LinuxIioAnalogInput.h"

#include <cassert>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

void makeDirectory(const std::string& path) {
  assert(mkdir(path.c_str(), 0700) == 0);
}

void writeFile(const std::string& path, const std::string& value) {
  std::ofstream output(path.c_str());
  assert(output);
  output << value;
  output.close();
  assert(output);
}

void removeTree(const std::string& path) {
  struct stat info;
  if (lstat(path.c_str(), &info) != 0) return;
  if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode)) {
    assert(unlink(path.c_str()) == 0);
    return;
  }

  DIR* directory = opendir(path.c_str());
  assert(directory != NULL);
  for (dirent* entry = readdir(directory); entry != NULL;
       entry = readdir(directory)) {
    const std::string name(entry->d_name);
    if (name == "." || name == "..") continue;
    removeTree(path + "/" + name);
  }
  closedir(directory);
  assert(rmdir(path.c_str()) == 0);
}

class Fixture {
public:
  Fixture() : root_(), iioRoot_(), ofNode_() {
    char pattern[] = "/tmp/oclock-iio-test-XXXXXXXX";
    char* root = mkdtemp(pattern);
    assert(root != NULL);
    root_ = root;
    iioRoot_ = root_ + "/iio";
    makeDirectory(iioRoot_);

    const std::string tree = root_ + "/device-tree";
    makeDirectory(tree);
    const std::string controller = tree + "/oclock-adc-spi";
    makeDirectory(controller);
    ofNode_ = controller + "/mcp3002@0";
    makeDirectory(ofNode_);
  }

  ~Fixture() { removeTree(root_); }

  std::string addDevice(const std::string& number, const std::string& name,
                        bool channel0 = true, bool channel1 = true,
                        const std::string& ofNode = std::string()) {
    const std::string device = iioRoot_ + "/iio:device" + number;
    makeDirectory(device);
    const std::string target = ofNode.empty() ? ofNode_ : ofNode;
    assert(symlink(target.c_str(), (device + "/of_node").c_str()) == 0);
    writeFile(device + "/name", name + "\n");
    if (channel0) writeFile(device + "/in_voltage0_raw", "123\n");
    if (channel1) writeFile(device + "/in_voltage1_raw", "987\n");
    return device;
  }

  const std::string& iioRoot() const { return iioRoot_; }
  const std::string& ofNode() const { return ofNode_; }

private:
  std::string root_;
  std::string iioRoot_;
  std::string ofNode_;
};

void testDiscoversByDeviceTreeAndReadsBothChannels() {
  Fixture fixture;
  const std::string device = fixture.addDevice("7", "mcp3002");
  std::unique_ptr<AnalogInput> input =
      createLinuxIioAnalogInput(fixture.iioRoot());
  assert(input->readAnalog(0) == -1);
  assert(input->initialize());
  assert(input->readAnalog(0) == 123);
  assert(input->readAnalog(1) == 987);
  assert(input->readAnalog(-1) == -1);
  assert(input->readAnalog(2) == -1);

  writeFile(device + "/in_voltage0_raw", "1023\n");
  assert(input->readAnalog(0) == 1023);
  writeFile(device + "/in_voltage0_raw", "1024\n");
  assert(input->readAnalog(0) == -1);
  writeFile(device + "/in_voltage0_raw", "12 trailing\n");
  assert(input->readAnalog(0) == -1);
}

void testRejectsWrongIdentityAndIncompleteChannels() {
  {
    Fixture fixture;
    fixture.addDevice("0", "wrong-name");
    std::unique_ptr<AnalogInput> input =
        createLinuxIioAnalogInput(fixture.iioRoot());
    assert(!input->initialize());
  }
  {
    Fixture fixture;
    fixture.addDevice("0", "mcp3002", true, false);
    std::unique_ptr<AnalogInput> input =
        createLinuxIioAnalogInput(fixture.iioRoot());
    assert(!input->initialize());
  }
}

void testRejectsMissingAndAmbiguousDeviceTreeMatches() {
  {
    Fixture fixture;
    const std::string unrelated = fixture.ofNode() + "-unrelated";
    makeDirectory(unrelated);
    fixture.addDevice("0", "mcp3002", true, true, unrelated);
    std::unique_ptr<AnalogInput> input =
        createLinuxIioAnalogInput(fixture.iioRoot());
    assert(!input->initialize());
  }
  {
    Fixture fixture;
    fixture.addDevice("0", "mcp3002");
    fixture.addDevice("9", "mcp3002");
    std::unique_ptr<AnalogInput> input =
        createLinuxIioAnalogInput(fixture.iioRoot());
    assert(!input->initialize());
  }
}

} // namespace

int main() {
  testDiscoversByDeviceTreeAndReadsBothChannels();
  testRejectsWrongIdentityAndIncompleteChannels();
  testRejectsMissingAndAmbiguousDeviceTreeMatches();
  std::cout << "IIO analog input tests passed\n";
  return 0;
}
