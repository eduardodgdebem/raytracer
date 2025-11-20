#pragma once
#include "hittable.h"
#include "material.h"
#include "thread_pool.h"

#include <atomic>
#include <future>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// -----------------------------
// Thread-local RNG helpers
// -----------------------------
inline std::mt19937 &thread_rng() {
  thread_local std::mt19937 rng([] {
    // seed using random_device and thread id to reduce collisions
    std::random_device rd;
    auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::seed_seq seq{rd(), static_cast<unsigned>(tid & 0xffffffffu)};
    return std::mt19937(seq);
  }());
  return rng;
}

inline double random_double_tls() {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(thread_rng());
}

inline vec3 random_in_unit_disk_tls() {
  while (true) {
    double x = 2.0 * random_double_tls() - 1.0;
    double y = 2.0 * random_double_tls() - 1.0;
    vec3 p(x, y, 0);
    if (dot(p, p) >= 1)
      continue;
    return p;
  }
}

// -----------------------------
// camera class (parallel render)
// -----------------------------
class camera {
public:
  double aspect_ratio = 1.0;  // Ratio of image width over height
  int image_width = 100;      // Rendered image width in pixel count
  int samples_per_pixel = 10; // Count of random samples for each pixel
  int max_depth = 10;         // Maximum number of rays bounces into scene
  color background;           // Scene background color

  double vfov = 90;                  // Vertical view angle (field of view)
  point3 lookfrom = point3(0, 0, 0); // Point camera is looking from
  point3 lookat = point3(0, 0, -1);  // Point camera is looking at
  vec3 vup = vec3(0, 1, 0);          // Camera-relative "up" direction

  double defocus_angle = 0; // Variation angle of rays through each pixel
  double focus_dist =
      10; // Distance from camera lookfrom point to plane of perfect focus

  void render(const hittable &world) {
    initialize();

    std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

    // choose thread count from hardware concurrency
    unsigned hw = std::thread::hardware_concurrency();
    unsigned thread_count = (hw == 0) ? 4u : hw; // fallback if unknown

    ThreadPool pool(thread_count);

    // framebuffer storing scaled color per pixel (R,G,B doubles in [0,1])
    std::vector<color> framebuffer(size_t(image_width) * size_t(image_height));

    std::vector<std::future<void>> futures;
    futures.reserve(image_height);

    std::atomic<int> rows_done{0};

    // Submit one task per row
    for (int j = 0; j < image_height; ++j) {
      futures.push_back(
          pool.enqueue([this, &world, j, &framebuffer, &rows_done]() {
            // Each thread should use thread-local RNG (helpers above)
            for (int i = 0; i < image_width; ++i) {
              color pixel_color(0, 0, 0);

              for (int sample = 0; sample < samples_per_pixel; ++sample) {
                ray r = get_ray_tls(i, j);
                pixel_color += ray_color(r, max_depth, world);
              }

              // scale by samples_per_pixel here (store already scaled)
              framebuffer[size_t(j) * size_t(image_width) + size_t(i)] =
                  pixel_color * pixel_samples_scale;
            }

            rows_done.fetch_add(1, std::memory_order_relaxed);
          }));
    }

    // While waiting, optionally print progress
    while (true) {
      bool all_done = true;
      for (auto &f : futures) {
        // poll futures: if any not ready, we are not done
        // std::future has no good poll, so check count instead
        // we'll use rows_done as progress indicator
      }
      int done = rows_done.load(std::memory_order_relaxed);
      std::clog << "\rRows done: " << done << " / " << image_height
                << std::flush;
      if (done >= image_height)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Ensure all tasks completed and rethrow exceptions if any
    for (auto &f : futures)
      f.get();

    std::clog << "\rDone computing. Writing...\n";

    // Write framebuffer in correct top-to-bottom order
    for (int j = 0; j < image_height; ++j) {
      for (int i = 0; i < image_width; ++i) {
        write_color(std::cout,
                    framebuffer[size_t(j) * size_t(image_width) + size_t(i)]);
      }
    }

    std::clog << "\rDone.                 \n";
  }

private:
  int image_height;           // Rendered image height
  double pixel_samples_scale; // Color scale factor for a sum of pixel samples
  point3 center;              // Camera center
  point3 pixel00_loc;         // Location of pixel 0, 0
  vec3 pixel_delta_u;         // Offset to pixel to the right
  vec3 pixel_delta_v;         // Offset to pixel below
  vec3 u, v, w;               // Camera frame basis vectors
  vec3 defocus_disk_u;        // Defocus disk horizontal radius
  vec3 defocus_disk_v;        // Defocus disk vertical radius

  void initialize() {
    image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    pixel_samples_scale = 1.0 / samples_per_pixel;

    center = lookfrom;

    auto theta = degrees_to_radians(vfov);
    auto h = tan(theta / 2);
    auto viewport_height = 2 * h * focus_dist;
    auto viewport_width =
        viewport_height * (double(image_width) / image_height);

    w = unit_vector(lookfrom - lookat);
    u = unit_vector(cross(vup, w));
    v = cross(w, u);

    vec3 viewport_u = viewport_width * u;
    vec3 viewport_v = viewport_height * -v;

    pixel_delta_u = viewport_u / image_width;
    pixel_delta_v = viewport_v / image_height;

    auto viewport_upper_left =
        center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
    pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

    auto defocus_radius =
        focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
    defocus_disk_u = u * defocus_radius;
    defocus_disk_v = v * defocus_radius;
  }

  // get_ray that uses thread-local RNG helpers (so sampling is thread-safe)
  ray get_ray_tls(int i, int j) const {
    vec3 offset(random_double_tls() - 0.5, random_double_tls() - 0.5, 0);
    auto pixel_sample = pixel00_loc + ((i + offset.x()) * pixel_delta_u) +
                        ((j + offset.y()) * pixel_delta_v);

    point3 ray_origin =
        (defocus_angle <= 0) ? center : defocus_disk_sample_tls();
    auto ray_direction = pixel_sample - ray_origin;
    auto ray_time = random_double_tls();

    return ray(ray_origin, ray_direction, ray_time);
  }

  vec3 sample_square() const {
    // keep original signature if other code calls it; but we prefer TLS
    return vec3(random_double_tls() - 0.5, random_double_tls() - 0.5, 0);
  }

  point3 defocus_disk_sample_tls() const {
    auto p = random_in_unit_disk_tls();
    return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
  }

  color ray_color(const ray &r, int depth, const hittable &world) const {
    if (depth <= 0)
      return color(0, 0, 0);

    hit_record rec;

    if (!world.hit(r, interval(0.001, infinity), rec))
      return background;

    ray scattered;
    color attenuation;
    color color_from_emission = rec.mat->emitted(rec.u, rec.v, rec.p);

    if (!rec.mat->scatter(r, rec, attenuation, scattered))
      return color_from_emission;

    color color_from_scatter =
        attenuation * ray_color(scattered, depth - 1, world);

    return color_from_emission + color_from_scatter;
  }
};
