#include <stdint.h>

namespace {

  inline uint64_t unpack(uint64_t color) {
    return color & 0xFFull |
        color << 8 & 0xFF0000ull |
        color << 16 & 0xFF00000000ull |
        color << 24 & 0xFF000000000000ull;
  }

  inline uint32_t pack(uint64_t unpacked) {
    return unpacked & 0xFFul |
        unpacked >> 8 & 0xFF00ul |
        unpacked >> 16 & 0xFF0000ul |
        unpacked >> 24 & 0xFF000000ul;
  }

  struct ImageBuffer {
    uint32_t width, height, pitch;
    uint32_t *pixels;
  };

  struct Rect {
    int32_t x1, y1, x2, y2;

    Rect() {}
    Rect(int32_t w, int32_t h): x1(0), y1(0), x2(w), y2(h) {}

    void intersect(const Rect &other) {
      if (x1 < other.x1) x1 = other.x1;
      if (y1 < other.y1) y1 = other.y1;
      if (x2 > other.x2) x2 = other.x2;
      if (y2 > other.y2) y2 = other.y2;
    }

    bool isEmpty() const {
      return x1 >= x2 && y1 >= y2;
    }

    int32_t getWidth() const {
      return x2 - x1;
    }

    int32_t getHeight() const {
      return y2 - y1;
    }

    void shift(int32_t x, int32_t y) {
      x1 += x;
      y1 += y;
      x2 += x;
      y2 += y;
    }
  };

  Rect clipInSource(const Rect &unclippedInTarget, const Rect &clippedInTarget) {
    Rect result;
    result.x1 = unclippedInTarget.x1 < 0 ? -unclippedInTarget.x1 : 0;
    result.y1 = unclippedInTarget.y1 < 0 ? -unclippedInTarget.y1 : 0;
    result.x2 = clippedInTarget.getWidth() + result.x1;
    result.y2 = clippedInTarget.getHeight() + result.y1;
    return result;
  }

  ImageBuffer* clipImageBuffer(ImageBuffer *buffer, const Rect &clip) {
    if (clip.isEmpty()) {
      buffer->width = 0;
      buffer->height = 0;
    } else {
      buffer->width = clip.getWidth();
      buffer->height = clip.getHeight();
      buffer->pixels += clip.x1 + buffer->pitch * clip.y1;
    }
    return buffer;
  }
}

extern "C" unsigned char __heap_base;
extern "C" uint32_t setMemorySize(uint32_t newSize);
extern "C" void dumpInt(int32_t val);

__attribute__ ((visibility("default"))) 
void *getHeapBase() {
  return &__heap_base;
}

namespace {
  struct LinearAllocator;

  class AllocatorGuard {
    LinearAllocator &allocator;
    uint32_t *ptr;
  public:
    AllocatorGuard(LinearAllocator &allocator, uint32_t *ptr): allocator(allocator), ptr(ptr) {}
    AllocatorGuard(AllocatorGuard &&other): allocator(other.allocator), ptr(other.ptr) {
      other.ptr = nullptr;
    }
    ~AllocatorGuard();
  };

  struct LinearAllocator {
    uint32_t *allocTop;
    uint32_t *lastKnownMemoryEnd;
  private:
    void refreshLastKnownMemoryEnd(uint32_t newSize=0) {
      lastKnownMemoryEnd = reinterpret_cast<uint32_t*>(setMemorySize(newSize));
    }
  public:

    uint32_t *allocateBytes(uint32_t numBytes) {
      return allocateWords((numBytes + 3) >> 2);
    }

    uint32_t *allocateWords(uint32_t numWords) {
      if (!allocTop) allocTop = static_cast<uint32_t*>(getHeapBase());
      if (!lastKnownMemoryEnd) refreshLastKnownMemoryEnd();
      uint32_t *ptr = allocTop;
      allocTop += numWords;
      if (allocTop > lastKnownMemoryEnd) {
        uint32_t memorySize = reinterpret_cast<uint32_t>(lastKnownMemoryEnd);
        uint32_t additional = numWords << 2;
        // we always grow by at least 4 MB
        if (additional < 4*1048576) additional = 4*1048576;
        setMemorySize(memorySize + additional);
        refreshLastKnownMemoryEnd();
      }
      return ptr;
    }

    AllocatorGuard guard() {
      return AllocatorGuard(*this, mark());
    }

    uint32_t *mark() {
      return allocTop;
    }

    void release(uint32_t *marked) {
      allocTop = marked;
    }
  } allocator = { nullptr, nullptr };

  AllocatorGuard::~AllocatorGuard() {
    if (ptr) allocator.release(ptr);
    ptr = nullptr;
  }
}

extern "C" ImageBuffer* createImageBuffer(uint32_t w, uint32_t h) {
  ImageBuffer *result = reinterpret_cast<ImageBuffer*>(allocator.allocateBytes(w * h * 4 + sizeof(ImageBuffer)));
  result->pitch = result->width = w;
  result->height = h;
  result->pixels = reinterpret_cast<uint32_t*>(result) + (sizeof(ImageBuffer) >> 2);
  return result;
}

extern "C" ImageBuffer* imageBufferShallowCopy(const ImageBuffer *buf) {
  ImageBuffer *result = reinterpret_cast<ImageBuffer*>(allocator.allocateBytes(sizeof(ImageBuffer)));
  result->width = buf->width;
  result->height = buf->height;
  result->pitch = buf->pitch;
  result->pixels = buf->pixels;
  return result;
}

extern "C" uint32_t getWidth(const ImageBuffer *img) {
  return img->width;
}

extern "C" uint32_t getHeight(const ImageBuffer *img) {
  return img->height;
}

extern "C" const uint32_t* getPixels(const ImageBuffer *img) {
  return img->pixels;
}

extern "C" uint32_t numPixels(const ImageBuffer *img) {
  return img->width * img->height;
}

extern "C" ImageBuffer* mip(const ImageBuffer *input) {
  const uint32_t *p = input->pixels;
  const uint32_t w = input->width;
  const uint32_t h = input->height;
  ImageBuffer *result = createImageBuffer((w+1) >> 1, (h+1) >> 1);
  uint32_t *output = result->pixels;
  for (int y = 0; y < h; y += 2) {
    for (int x = 0; x < w; x += 2) {
      uint64_t sum = unpack(p[0]);
      int right = x + 1 < w ? 1 : 0;
      int down = y + 1 < h ? w : 0;
      sum += unpack(p[right]);
      sum += unpack(p[down]);
      sum += unpack(p[right + down]);
      *output++ = pack(sum >> 2);
      p += right ? 2 : 1;
    }
    p += w;
  }
  return result;
}

extern "C" uint32_t overlapScore(const ImageBuffer *target, const ImageBuffer *source, int32_t ox, int32_t oy) {
  auto guard = allocator.guard();

  Rect unclippedInTarget = Rect(source->width, source->height);
  unclippedInTarget.shift(ox, oy);
  Rect clippedInTarget(target->width, target->height);
  clippedInTarget.intersect(unclippedInTarget);
  Rect clippedInSource = clipInSource(unclippedInTarget, clippedInTarget);
  ImageBuffer *clippedSource = clipImageBuffer(imageBufferShallowCopy(source), clippedInSource);
  ImageBuffer *clippedTarget = clipImageBuffer(imageBufferShallowCopy(target), clippedInTarget);
  return (clippedSource->width ^ clippedTarget->width) | (clippedSource->height ^ clippedTarget->height);
}
