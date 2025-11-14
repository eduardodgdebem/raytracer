#include <print>

int main() {
  int image_width = 256;
  int image_height = 256;

  std::print("P3/n{} {}\n255\n", image_width, image_height);

  for (int i{}; i < image_height; ++i) {
    for (int j{}; j < image_height; ++j) {
      auto r = double(j) / (image_width - 1);
      auto g = double(i) / (image_height - 1);
      auto b = 0.0;

      int ir = int(255.999 * r);
      int ig = int(255.999 * g);
      int ib = int(255.999 * b);

      std::print("{} {} {}\n", ir, ig, ib);
    }
  }
}
