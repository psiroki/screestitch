import dropHandler from "./drop.js";
import ScrollZoom from "./ui/scrollzoom.js";
import { element, downloadCanvas } from "./ui/elements.js";

const imageCollectionDiv = document.querySelector(".imageCollection");
const images = [];
const thisScript = import.meta.url;
const testMode = false;

function resolveScript(uri) {
  if (!/^\.{0,2}\//.test(uri)) {
    uri = "./"+uri;
  }
  return new URL(uri, thisScript).toString();
}

class SelectableImage {
  constructor(imageBitmap) {
    this.imageBitmap = imageBitmap;
    this.canvas = document.createElement("canvas");
    this.canvas.width = imageBitmap.width;
    this.canvas.height = imageBitmap.height;
    this.context = this.canvas.getContext("2d");
    this.context.globalCompositeOperation = "copy";
    this.context.drawImage(this.imageBitmap, 0, 0);
    this._selected = false;
    this.canvas.addEventListener("click", _ => {
      this.selected = !this.selected;
    });
  }

  _syncSelected() {
    this.canvas.classList.toggle("selected", this._selected);
  }

  get selected() {
    return this._selected;
  }

  set selected(val) {
    this._selected = !!val;
    this._syncSelected();
  }
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

async function addImage(blob) {
  const image = await createImageBitmap(blob);
  const img = new SelectableImage(image);
  img.selected = true;
  images.push(img);
  imageCollectionDiv.append(img.canvas);
}

document.querySelector("input[type=file]").addEventListener("input", e => {
  let files = Array.from(e.target.files);
  e.target.value = "";
  for (let file of files) {
    addImage(file);
  }
});

dropHandler(document.body, blob => { addImage(blob); }, () => {});

const worker = new Worker(resolveScript("stitch_worker.js"), { type: "module" });

function stitch() {
  let sel = images.filter(e => e.selected).map(e => e.imageBitmap);
  if (sel.length === 2) {
    let progressBar = document.createElement("progress");
    progressBar.removeAttribute("value");
    document.body.append(progressBar);
    let c = new MessageChannel();
    let start = 0;
    let expectedLength = -1;
    let maxProgress = 0;
    let working = true;
    let refreshProgress;
    refreshProgress = () => {
      if (start && expectedLength > 0) {
        let now = performance.now();
        progressBar.max = expectedLength;
        progressBar.value = now - start;
      }
      if (working) {
        requestAnimationFrame(refreshProgress);
      } else {
        progressBar.remove();
      }
    };
    c.port2.onmessage = e => {
      let msg = e.data;
      if (msg.result) {
        working = false;
        let view, controls;
        let canvas = new SelectableImage(e.data.result).canvas;
        let slider, valueDisplay;
        let resultDiv = element("div", [
          view = element("div", [canvas], "view"),
          controls = element("div", [
            element("button", "PNG", null, _ => {
              downloadCanvas("image_.png", canvas, "image/png");
            }),
            element("button", "JPEG", null, _ => {
              downloadCanvas("image_.jpg", canvas, "image/jpeg", +slider.value);
            }),
            slider = element("input", [], {
              type: "range",
              min: 0,
              max: 1,
              step: 0.01,
              value: 0.7,
            }, {
              input: _ => {
                valueDisplay.textContent = (+slider.value*100)+"%";
              },
            }),
            valueDisplay = element("span", "70%"),
          ], "controls"),
        ], "result");
        document.body.append(resultDiv);
        new ScrollZoom(view);
      }
      if (msg.progress) {
        let now = performance.now();
        let progress = msg.progress;
        if (!start) start = now;
        if (!maxProgress) maxProgress = msg.progress;
        if (progress !== maxProgress) {
          expectedLength = (now - start) / (maxProgress - progress) * maxProgress;
        }
      }
    };
    worker.postMessage({ bitmaps: [...sel], port: c.port1 }, [...sel, c.port1]);
    refreshProgress();
  }
}

document.querySelector("button[type=button]").addEventListener("click", stitch);

if (testMode) {
  Promise.all([
    fetch("1.png").then(r => r.blob()).then(blob => addImage(blob)),
    fetch("2.png").then(r => r.blob()).then(blob => addImage(blob)),
  ]).then(_ => stitch());
}
