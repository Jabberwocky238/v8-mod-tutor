#include <iostream>

#include "engine.h"

int main(int argc, char** argv) {
  Engine engine(argv[0]);
  std::cout << engine.evaluate("'Hello' + ', World!'") << '\n';
  return 0;
}
