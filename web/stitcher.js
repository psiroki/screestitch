function loadImage(url) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = _ => {
      resolve(img);
    };
    img.onerror = e => {
      reject(e);
    };
    img.src = url;
  });
}

function getImageData(img) {
  const c = new OffscreenCanvas(img.naturalWidth, img.naturalHeight);
  const ctx = c.getContext("2d");
  ctx.globalCompositeOperation = "copy";
  ctx.drawImage(img, 0, 0);
  return ctx.getImageData(0, 0, c.width, c.height);
}

function dumpImageData(imgData) {
  const c = document.createElement("canvas");
  c.width = imgData.width;
  c.height = imgData.height;
  const ctx = c.getContext("2d");
  ctx.globalCompositeOperation = "copy";
  ctx.putImageData(imgData, 0, 0);
  document.body.append(c);
  return c;
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

async function loadWasmAndRun() {
  // Load the wasm module
  const imagePromises = ["1.png", "2.png"].map(loadImage);
  const response = await fetch("screestitch.wasm");
  let memory;
  let overlap = [];
  let phase = -1;
  let quadInt;
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
      quadInt = [a, b, c, d];
    },
    phase(p) {
      phase = p;
    },
    dumpScore(score, x, y, w, h, a, b) {
      overlap = { score, x, y, w, h, a, b };
    },
    reportProgress(progress) {
      console.log(progress);
    }
  }});
  const images = (await Promise.all(imagePromises)).map(getImageData);
  const instance = instanceAndModule.instance;
  memory = instance.exports.memory;
  const bytesRequired = images.reduce((val, imageData) => val + imageData.width * imageData.height, 0) * 6;
  const mip = instance.exports.mip;
  const createImageBuffer = instance.exports.createImageBuffer;
  const getPitch = instance.exports.getPitch;
  const getWidth = instance.exports.getWidth;
  const getHeight = instance.exports.getHeight;
  const getPixels = instance.exports.getPixels;
  const numPixels = instance.exports.numPixels;
  const findOverlap = instance.exports.findOverlap;
  const createMipChain = instance.exports.createMipChain;
  const getMipChainSize = instance.exports.getMipChainSize;
  const getMipMap = instance.exports.getMipMap;

  function imageDataOf(imageBuffer) {
    const pixels = new Uint32Array(memory.buffer, getPixels(imageBuffer), numPixels(imageBuffer));
    return createImageData(pixels, getWidth(imageBuffer), getHeight(imageBuffer), getPitch(imageBuffer));
  }

  function imageDataToBuffer(imageData) {
    let { width, height } = imageData;
    let image = createImageBuffer(width, height);
    new Uint8Array(memory.buffer, getPixels(image), 4*numPixels(image)).set(imageData.data);
    return image;
  }

  function generateMipmaps(imageData) {
    let { width, height } = imageData;
    let lastImage = createImageBuffer(width, height);
    new Uint8Array(memory.buffer, getPixels(lastImage), 4*numPixels(lastImage)).set(imageData.data);
    let level = 1;
    while (width > 1 && height > 1) {
      const nextImage = mip(lastImage);
      const nextWidth = getWidth(nextImage);
      const nextHeight = getHeight(nextImage);
      dumpImageData(imageDataOf(nextImage)).title = `Level ${level}: ${nextWidth}x${nextHeight}`;
      lastImage = nextImage;
      width = nextWidth;
      height = nextHeight;
      ++level;
    }
  }

  let start = performance.now();
  try {
    console.log(Number(findOverlap(imageDataToBuffer(images[0]), imageDataToBuffer(images[1]))).toExponential());
  } finally {
    console.log(quadInt);
    console.log(phase, overlap, [overlap.a, overlap.b].map(e => [getWidth(e), getHeight(e), getPixels(e), getPixels(e) + getHeight(e) * getPitch(e)]));
  }
  console.log(performance.now() - start);
  overlap && console.log(images[0], Number(overlap.score).toExponential(), overlap.x, overlap.y, overlap.w+"x"+overlap.h);
  dumpImageData(imageDataOf(overlap.a)).setAttribute("style", "opacity: 0.5; position: absolute; left: 10px; top: 10px;");
  dumpImageData(imageDataOf(overlap.b)).setAttribute("style", `opacity: 0.5; position: absolute; left: ${overlap.x+10}px; top: ${overlap.y+10}px;`);
  document.body.setAttribute("style", "transform-origin: 0 0; transform: scale(0.5);");
  //  generateMipmaps(images[0]);
//  generateMipmaps(images[1]);
}

console.log("1..");
loadWasmAndRun();
