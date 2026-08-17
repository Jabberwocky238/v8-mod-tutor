#include <iostream>

#include "engine.h"

int main(int argc, char** argv) {
  Engine engine(argv[0]);
  auto result = engine.evaluate("'Hello' + ', World!'");
  std::cout << result.cStr() << '\n';
  return 0;
}
