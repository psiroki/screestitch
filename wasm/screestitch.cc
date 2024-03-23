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
    union {
      struct {
        int32_t x1, y1, x2, y2;
      };
      int32_t coords[4];
    };

    Rect() {}
    Rect(int32_t w, int32_t h): x1(0), y1(0), x2(w), y2(h) {}

    void intersect(const Rect &other) {
      if (x1 < other.x1) x1 = other.x1;
      if (y1 < other.y1) y1 = other.y1;
      if (x2 > other.x2) x2 = other.x2;
      if (y2 > other.y2) y2 = other.y2;
    }

    void extend(const Rect &other) {
      if (x1 > other.x1) x1 = other.x1;
      if (y1 > other.y1) y1 = other.y1;
      if (x2 < other.x2) x2 = other.x2;
      if (y2 < other.y2) y2 = other.y2;
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

  inline uint32_t channelSimilarityScore(uint32_t a, uint32_t b, uint32_t shift) {
    uint32_t as = a >> shift & 0xff;
    uint32_t bs = b >> shift & 0xff;
    return 0xff - (as > bs ? as - bs : bs - as);
  }

  uint32_t pixelSimilarityScore(uint32_t a, uint32_t b) {
    uint32_t score = channelSimilarityScore(a, b, 0);
    score += channelSimilarityScore(a, b, 8);
    score += channelSimilarityScore(a, b, 16);
    // we are ignoring the alpha channel
    return score;
  }

  int numBitsUsed(uint32_t bits) {
    int result = 0;
    while (bits) {
      bits >>= 1;
      ++result;
    }
    return result;
  }

  struct MipChain {
    uint32_t size, capacity;
    ImageBuffer **buffers;

    void add(ImageBuffer *ptr) {
      buffers[size++] = ptr;
    }

    ImageBuffer* get(int index) {
      return buffers[index];
    }
  };

  inline uint32_t ablend(uint32_t col, uint8_t alpha) {
    uint64_t v = 
      (((col & 0xff0000ULL) << 16) |
      ((col & 0xff00ULL) << 8) |
      col & 0xffULL) * alpha;
    return ((v >> 24) & 0xff0000) |
      ((v >> 16) & 0xff00) |
      ((v >> 8) & 0xff);
  }
}

extern "C" unsigned char __heap_base;
extern "C" uint32_t setMemorySize(uint32_t newSize);
extern "C" void dumpInt(int32_t val);
extern "C" void dumpQuadInt(int64_t a, int64_t b, int64_t c, int64_t d);
extern "C" void dumpScore(uint64_t score, int32_t x, int32_t y, uint32_t w, uint32_t h, const void *a, const void *b);
extern "C" void reportProgress(uint32_t remaining);

static void dumpPointer(const void *p) {
  dumpInt(reinterpret_cast<uint32_t>(p));
}

static void dumpPointerDelta(const void *a, const void *b) {
  dumpInt(reinterpret_cast<uint32_t>(b)-reinterpret_cast<uint32_t>(a));
}

static inline void *getHeapBase() {
  return &__heap_base;
}

namespace {
  static const uint32_t allocBy = 256*1024*1024;
  static const uint32_t *zeroAddress = nullptr;

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
    void release();
  };

  struct LinearAllocator {
    uint32_t *allocTop;
    uint32_t *lastKnownMemoryEnd;
  private:
    void refreshLastKnownMemoryEnd(uint32_t newSize=0) {
      lastKnownMemoryEnd = reinterpret_cast<uint32_t*>(setMemorySize(newSize));
    }
  public:
    const uint32_t* top() const {
      return allocTop;
    }

    void reset() {
      allocTop = static_cast<uint32_t*>(getHeapBase());
    }

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
        // we always grow by at least allocBy
        if (additional < allocBy) additional = allocBy;
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
    release();
  }

  void AllocatorGuard::release() {
    if (ptr) allocator.release(ptr);
    ptr = nullptr;
  }

  MipChain* allocateMipChain(uint32_t capacity) {
    const uint32_t *start = allocator.top();
    uint32_t numWords = (sizeof(MipChain) + capacity * sizeof(ImageBuffer*)) >> 2;
    uint32_t *raw = allocator.allocateWords(numWords);
    const uint32_t *end = allocator.top();
    for (int i = 0; i < numWords; ++i)
      raw[i] = 0;
    MipChain *mc = reinterpret_cast<MipChain*>(raw);
    mc->buffers = reinterpret_cast<ImageBuffer**>(raw + (sizeof(MipChain) >> 2));
    mc->capacity = capacity;
    return mc;
  }
}

extern "C" void reset() {
  allocator.reset();
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

extern "C" uint32_t getMipChainSize(const MipChain *mc) {
  return mc->size;
}

extern "C" const ImageBuffer* getMipMap(const MipChain *mc, uint32_t index) {
  return mc->buffers[index];
}

extern "C" uint32_t getPitch(const ImageBuffer *img) {
  return img->pitch;
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
  const uint32_t p2 = input->pitch * 2;
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
    p += p2 - w;
  }
  return result;
}

static void dumpRect(const Rect &r) {
  dumpQuadInt(r.x1, r.y1, r.x2, r.y2);
}

extern "C" uint64_t overlapScore(const ImageBuffer *target, const ImageBuffer *source, int32_t ox, int32_t oy) {
  auto guard = allocator.guard();

  Rect unclippedInTarget = Rect(source->width, source->height);
  unclippedInTarget.shift(ox, oy);
  Rect clippedInTarget(target->width, target->height);
  clippedInTarget.intersect(unclippedInTarget);
  Rect clippedInSource = clipInSource(unclippedInTarget, clippedInTarget);
  ImageBuffer *clippedSource = clipImageBuffer(imageBufferShallowCopy(source), clippedInSource);
  ImageBuffer *clippedTarget = clipImageBuffer(imageBufferShallowCopy(target), clippedInTarget);
  const uint32_t w = clippedSource->width;
  const uint32_t h = clippedSource->height;
  const uint32_t sp = clippedSource->pitch;
  const uint32_t tp = clippedTarget->pitch;
  const uint32_t *sourceLine = clippedSource->pixels;
  const uint32_t *targetLine = clippedTarget->pixels;
  uint64_t score = 0;
  for (uint32_t y = 0; y < h; ++y) {
    uint32_t lastSource = *sourceLine;
    uint32_t lastTarget = *targetLine;
    for (uint32_t x = 0; x < w; ++x) {
      uint32_t thisSource = sourceLine[x];
      uint32_t thisTarget = targetLine[x];
      if (pixelSimilarityScore(lastSource, thisSource) < (255-32)*3 &&
          pixelSimilarityScore(lastTarget, thisTarget) < (255-32)*3) {
        score += pixelSimilarityScore(thisSource, thisTarget);
      }
      lastSource = thisSource;
      lastTarget = thisTarget;
    }
    sourceLine += sp;
    targetLine += tp;
  }
  return score;
}

static int mipsRecommended(const ImageBuffer *a, int minimumSizeMagnitude) {
  int wb = numBitsUsed(a->width);
  int hb = numBitsUsed(a->height);
  int b = wb > hb ? hb : wb;
  return b > minimumSizeMagnitude ? b - minimumSizeMagnitude : 1;
}

static int mipsRecommendedCombined(const ImageBuffer *a, const ImageBuffer *b, int minimumSizeMagnitude) {
  int am = mipsRecommended(a, minimumSizeMagnitude);
  int bm = mipsRecommended(b, minimumSizeMagnitude);
  return am > bm ? bm : am;
}

extern "C" MipChain* createMipChain(const ImageBuffer *a, int numMips) {
  MipChain *chain = allocateMipChain(numMips);
  chain->add(const_cast<ImageBuffer*>(a));
  const ImageBuffer *buf = chain->get(1);
  while (chain->size < chain->capacity) {
    ImageBuffer *mipped = mip(chain->buffers[chain->size - 1]);
    chain->add(mipped);
  }
  return chain;
}

extern "C" ImageBuffer* findOverlap(const ImageBuffer *a, const ImageBuffer *b) {
  auto guard = allocator.guard();
  uint32_t remaining = (((numPixels(a) + numPixels(b)) >> 8) * 3) >> 1;
  reportProgress(remaining + 1);
  int numMips = mipsRecommendedCombined(a, b, 3);
  MipChain *ac = createMipChain(a, numMips);
  MipChain *bc = createMipChain(b, numMips);
  uint64_t maxScore = 0;
  int maxScoreX = 0;
  int maxScoreY = 0;
  reportProgress(remaining);
  for (int i = numMips - 1; i >= 0; --i) {
    ImageBuffer *am = ac->get(i);
    ImageBuffer *bm = bc->get(i);
    int x1 = maxScore ? maxScoreX*2-1 : -bm->width + 1;
    int y1 = maxScore ? maxScoreY*2-1 : -bm->height + 1;
    int x2 = maxScore ? maxScoreX*2+2 : am->width;
    int y2 = maxScore ? maxScoreY*2+2 : am->height;
    maxScore = 0;
    for (int y = y1; y < y2; ++y) {
      for (int x = x1; x < x2; ++x) {
        uint64_t score = overlapScore(am, bm, x, y);
        if (score > maxScore) {
          maxScore = score;
          maxScoreX = x;
          maxScoreY = y;
          dumpScore(maxScore, x, y, am->width, am->height, am, bm);
        }
      }
    }
    uint32_t processed = (numPixels(am) + numPixels(bm)) >> 8;
    remaining = remaining > processed ? remaining - processed : 0;
    reportProgress(remaining);
  }
  Rect ar(a->width, a->height);
  Rect br(b->width, b->height);
  Rect abr(ar);
  br.shift(maxScoreX, maxScoreY);
  abr.extend(br);
  int restoreX = maxScoreX < 0 ? -maxScoreX : 0;
  int restoreY = maxScoreY < 0 ? -maxScoreY : 0;
  ar.shift(restoreX, restoreY);
  br.shift(restoreX, restoreY);
  Rect overlap(ar);
  overlap.intersect(br);

  guard.release();

  ImageBuffer *result = createImageBuffer(abr.getWidth(), abr.getHeight());
  uint32_t numBytes = result->width * result->pitch;
  for (uint32_t i = 0; i < numBytes; ++i)
    result->pixels[i] = 0;
  
  auto resultGuard = allocator.guard();
  
  ImageBuffer *arc = clipImageBuffer(imageBufferShallowCopy(result), ar);
  const uint32_t *srcLine = a->pixels;
  uint32_t *dstLine = arc->pixels;
  for (int y = 0; y < a->height; ++y) {
    for (int x = 0; x < a->width; ++x) {
      dstLine[x] = srcLine[x];
    }
    srcLine += a->pitch;
    dstLine += arc->pitch;
  }

  Rect arInBr(ar);
  overlap.shift(-br.x1, -br.y1);
  arInBr.shift(-br.x1, -br.y1);
  // v points from the overlap center to the other image center
  int32_t vx = (arInBr.x1 + arInBr.x2) / 2 - (overlap.x1 + overlap.x2) / 2;
  int32_t vy = (arInBr.y1 + arInBr.y2) / 2 - (overlap.y1 + overlap.y2) / 2;
  int64_t minDot = INT64_MAX;
  int64_t maxDot = INT64_MIN;
  for (int i = 0; i < 4; ++i) {
    int32_t x = overlap.coords[(i & 1) << 1];
    int32_t y = overlap.coords[i | 1];
    int64_t dot = static_cast<int64_t>(vx) * x + static_cast<int64_t>(vy) * y;
    if (dot < minDot) minDot = dot;
    if (dot > maxDot) maxDot = dot;
  }
  int64_t dotDiff = maxDot - minDot;

  ImageBuffer *brc = clipImageBuffer(imageBufferShallowCopy(result), br);
  srcLine = b->pixels;
  dstLine = brc->pixels;
  for (int y = 0; y < b->height; ++y) {
    if (y >= overlap.y1 && y < overlap.y2) {
      int64_t dotBase = static_cast<int64_t>(vy) * y;
      for (int x = 0; x < overlap.x1; ++x) {
        dstLine[x] = srcLine[x];
      }
      for (int x = overlap.x1; x < overlap.x2; ++x) {
        int64_t rawDot = dotBase + static_cast<int64_t>(vx) * x;
        int32_t dot = (rawDot - minDot) * 256 / dotDiff;
        if (dot >= 255) continue;
        if (dot <= 0) {
          dstLine[x] = srcLine[x];
        } else {
          dstLine[x] = ablend(dstLine[x], dot) + ablend(srcLine[x], 255-dot) | 0xff000000u;
        }
      }
      for (int x = overlap.x2; x < b->width; ++x) {
        dstLine[x] = srcLine[x];
      }
    } else {
      for (int x = 0; x < b->width; ++x) {
        dstLine[x] = srcLine[x];
      }
    }
    srcLine += b->pitch;
    dstLine += brc->pitch;
  }

  return result;
}
