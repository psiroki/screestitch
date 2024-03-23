import crc32 from "../common/crc32.js";

function makeArray(e) {
  return e instanceof Array ? e : [e];
}

export function element(name, contents, classOrAttributes=null, handlersOrClick=null) {
  const commitButton = name.toLowerCase() === "commitbutton";
  name = commitButton ? "button" : name;
  const e = document.createElement(name);
  if (name.toLowerCase() === "button")
    e.type = "button";
  if (typeof classOrAttributes === "string") e.className = classOrAttributes;
  if (classOrAttributes && typeof classOrAttributes === "object") {
    for (let a of Object.entries(classOrAttributes)) {
      let n = a[0];
      if (n === "className") n = "class";
      e.setAttribute(n, a[1]);
    }
  }
  if (commitButton) e.classList.add("commitButton");
  if (handlersOrClick) {
    if (typeof handlersOrClick === "function") handlersOrClick = { click: handlersOrClick };
    for (let handler of Object.entries(handlersOrClick)) {
      let eventName = handler[0];
      let fun = handler[1];
      let runListener = function (...args) {
        const result = fun.apply(this, args);
        if (result instanceof Promise) {
          return;
        } else {
          return result;
        }
      };
      if (commitButton && eventName === "click") {
        let downEvent = null;
        e.addEventListener("pointerdown", e => {
          if (e.isPrimary) downEvent = e;
        });
        const cancel = e => {
          if (e.isPrimary) downEvent = null;
        };
        for (let eventName of ["pointerleave", "pointercancel"]) {
          e.addEventListener(eventName, cancel);
        }
        e.addEventListener("pointerup", function(e) {
          if (e.isPrimary && downEvent) {
            let length = e.timeStamp - downEvent.timeStamp;
            if (length >= 2000) return runListener.call(this);
          }
        });
      } else {
        e.addEventListener(eventName, runListener);
      }
    }
  }
  if (contents !== null && typeof contents !== "undefined") {
    e.append(...makeArray(contents));
  }
  return e;
}

export function loadImage(url) {
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

export function canvasToBlob(canvas, contentType, encoderOptions) {
	return new Promise(resolve => {
		canvas.toBlob(resolve, contentType, encoderOptions);
	});
}

function formatCrc(crc) {
	if (crc < 0) {
		crc = (crc >>> 4).toString(16)+(crc & 0xf).toString(16);
	} else {
		crc = crc.toString(16);
	}
	return "00000000".substring(crc.length)+crc;
}

function readBlobAsBytes(blob) {
	if (!blob) throw "Blob must not be null(ish)";
	return new Promise((resolve, reject) => {
		var reader = new FileReader();
		reader.onload = function() {
			if (reader.result instanceof ArrayBuffer) {
				resolve(new Uint8Array(reader.result));
			} else {
				resolve(reader.result);
			}
		};
		reader.onerror = function() {
			reject("error loading file");
		};
		reader.readAsArrayBuffer(blob, "UTF-8");
	});
}

export async function downloadCanvas(downloadName, canvas, contentType, encoderOptions) {
	let blob = await canvasToBlob(canvas, contentType, encoderOptions);
  let crc = formatCrc(crc32(await readBlobAsBytes(blob)));
  downloadName = downloadName.replace(/\.([^\.]+)$/, crc+".$1");
	let blobUrl = URL.createObjectURL(blob);
	let a = document.createElement("a");
	a.href = blobUrl;
	a.download = downloadName;
	a.click();
	URL.revokeObjectURL(blobUrl);
}
