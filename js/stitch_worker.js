const thisScript = import.meta.url;

function resolveScript(uri) {
  if (!/^\.{0,2}\//.test(uri)) {
    uri = "./"+uri;
  }
  return new URL(uri, thisScript).toString();
}


function createImageData(typedArray, width, height, pitch = 0) {
  let arr;
  if (pitch && pitch !== width) {
    arr = new Uint8ClampedArray(width * height * 4);
    const unpacked = new Uint8ClampedArray(typedArray.buffer, typedArray.byteOffset, typedArray.byteLength);
    let offset = 0;
    for (let y = 0; y < height; ++y) {
      arr.set(unpacked.subarray(offset, offset + width*4), y * width * 4)
      offset += pitch*4;
    }
  } else {
    arr = new Uint8ClampedArray(typedArray.buffer, typedArray.byteOffset, typedArray.byteLength);
  }
  return new ImageData(arr, width, height);
}

async function startService() {
  // Load the wasm module
  const response = await fetch(resolveScript("../screestitch.wasm"));
  let memory;
  let overlap = [];
  let progressStart = 0;
  let currentProgressListener = _ => {};
  const reportProgress = progress => {
    console.log(performance.now() - progressStart, progress);
    currentProgressListener(progress);
  };
  const instanceAndModule = await WebAssembly.instantiateStreaming(response, { env: {
    setMemorySize(bytes) {
      if (bytes) {
        let delta = bytes - memory.buffer.byteLength;
        if (delta > 0) {
          const pages = (delta + 0xFFFF) >> 16;
          console.log("Growing by ", pages*64);
          memory.grow(pages);
        }
        console.log("kbytesRequired: ", bytes+1023 >> 10, " overall available: ", memory.buffer.byteLength+1023 >> 10);
      }
      return memory.buffer.byteLength;
    },
    dumpInt(val) {
      console.log(val);
    },
    dumpQuadInt(a, b, c, d) {
      console.log(a, b, c, d);
    },
    dumpScore(score, x, y, w, h, a, b) {
      overlap = { score, x, y, w, h, a, b };
    },
    reportProgress,
  }});
  const instance = instanceAndModule.instance;
  memory = instance.exports.memory;
  const mip = instance.exports.mip;
  const createImageBuffer = instance.exports.createImageBuffer;
  const reset = instance.exports.reset;
  const getPitch = instance.exports.getPitch;
  const getWidth = instance.exports.getWidth;
  const getHeight = instance.exports.getHeight;
  const getPixels = instance.exports.getPixels;
  const numPixels = instance.exports.numPixels;
  const findOverlap = instance.exports.findOverlap;

  async function bufferToImageBitmap(imageBuffer) {
    const pixels = new Uint32Array(memory.buffer, getPixels(imageBuffer), numPixels(imageBuffer));
    return await createImageBitmap(createImageData(pixels, getWidth(imageBuffer), getHeight(imageBuffer), getPitch(imageBuffer)));
  }

  function imageBitmapToBuffer(imageBitmap) {
    const context = new OffscreenCanvas(imageBitmap.width, imageBitmap.height).getContext("2d");
    context.globalCompositeOperation = "copy";
    context.drawImage(imageBitmap, 0, 0);
    const imageData = context.getImageData(0, 0, imageBitmap.width, imageBitmap.height);
    let { width, height } = imageData;
    let image = createImageBuffer(width, height);
    new Uint8Array(memory.buffer, getPixels(image), 4*numPixels(image)).set(imageData.data);
    return image;
  }

  return {
    async overlap(imageBitmap1, imageBitmap2, port) {
      reset();
      let i1 = imageBitmapToBuffer(imageBitmap1);
      let i2 = imageBitmapToBuffer(imageBitmap2);
      progressStart = performance.now();
      try {
        if (port) {
          currentProgressListener = progress => {
            port.postMessage({ progress });
          };
        }
        return await bufferToImageBitmap(findOverlap(i1, i2));
      } finally {
        currentProgressListener = _ => {};
      }
    }
  };
}

const servicePromise = startService();

self.addEventListener("message", e => {
  servicePromise.then(service => service.overlap(e.data.bitmaps[0], e.data.bitmaps[1], e.data.port))
    .then(stitchedImage => e.data.port.postMessage({ result: stitchedImage }, [stitchedImage]));
});
