#include <iostream>
#include <print>

#include "color.h"

int main() {
  int image_width = 256;
  int image_height = 256;

  std::print("P3/n{} {}\n255\n", image_width, image_height);

  for (int i{}; i < image_height; ++i) {
    std::clog << "\rScanlines remaining: " << (image_height - i) << ' '
              << std::flush;
    for (int j{}; j < image_height; ++j) {
      auto pixel_color = color(double(i) / (image_width - 1),
                               double(j) / (image_height - 1), 0);
      write_color(std::cout, pixel_color);
    }
  }

  std::clog << "\rDone.                 \n";
}
