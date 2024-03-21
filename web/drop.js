const dropHandler = (element, callback, hoverCallback) => {
  const api = {};

  var dragging = false;
  var dropX = null;
  var dropY = null;
  var dragEndListeners = [];

  function handleDataTransferFiles(dataTransfer, event) {
    var files = Array.prototype.slice.call(dataTransfer.files);
    var foundFiles = false;
    files.forEach(function (f) {
      foundFiles = true;
      callback(f, event, api);
    });
    return foundFiles;
  }

  function b(x) {
    return typeof x === "undefined" || x === null || x;
  }

  function dragOver(e) {
    e.dataTransfer.dropEffect = "copy";
    dropX = e.clientX;
    dropY = e.clientY;
    dragging = true;
    if (!hoverCallback || b(hoverCallback(api))) {
      e.stopPropagation();
      e.preventDefault();
    } else {
      dragging = false;
    }
    this.classList.toggle("dragOver", dragging);
  }

  function drop(e) {
    handleDataTransferFiles(e.dataTransfer, e);
    dragging = false;
    if (!hoverCallback || b(hoverCallback(api))) {
      e.stopPropagation();
      e.preventDefault();
    }
    this.classList.remove("dragOver");
    dropX = dropY = null;
  }

  function dragEnter(e) {
    dragging = true;
    dropX = e.clientX;
    dropY = e.clientY;
    if (hoverCallback && !b(hoverCallback(api))) {
      dragging = false;
    }
    this.classList.toggle("dragOver", dragging);
  }

  function dragLeave(e) {
    this.classList.remove("dragOver");
    dragging = false;
    dropX = dropY = null;
    if (hoverCallback) hoverCallback(api);
  }

  var bound = false;
  api.bind = () => {
    bound = true;
    element.addEventListener("dragover", dragOver, false);
    element.addEventListener("drop", drop, false);
    element.addEventListener("dragenter", dragEnter, false);
    element.addEventListener("dragleave", dragLeave, false);
  };

  api.unbind = () => {
    if (!bound) return;
    element.removeEventListener("dragover", dragOver, false);
    element.removeEventListener("drop", drop, false);
    element.removeEventListener("dragenter", dragEnter, false);
    element.removeEventListener("dragleave", dragLeave, false);
    bound = false;
    dragging = false;
  };

  Object.defineProperty(api, "dragging", { get: () => dragging });
  Object.defineProperty(api, "dropX", { get: () => dropX });
  Object.defineProperty(api, "dropY", { get: () => dropY });

  api.bind();

  return api;
};
