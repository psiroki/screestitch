export function matrixArrayToCssMatrix(array) {
  return "matrix3d(" + array.join(",") + ")";
}
export function multiplyMatrixAndVector(matrix, vector) {
  //Give a simple variable name to each part of the matrix, a column and row number
  const c0r0 = matrix[0], c1r0 = matrix[1], c2r0 = matrix[2], c3r0 = matrix[3];
  const c0r1 = matrix[4], c1r1 = matrix[5], c2r1 = matrix[6], c3r1 = matrix[7];
  const c0r2 = matrix[8], c1r2 = matrix[9], c2r2 = matrix[10], c3r2 = matrix[11];
  const c0r3 = matrix[12], c1r3 = matrix[13], c2r3 = matrix[14], c3r3 = matrix[15];
  //Now set some simple names for the vector
  const x = vector[0];
  const y = vector[1];
  const z = vector[2];
  const w = vector[3];
  //Multiply the vector against each part of the 1st column, then add together
  const resultX = x * c0r0 + y * c0r1 + z * c0r2 + w * c0r3;
  //Multiply the vector against each part of the 2nd column, then add together
  const resultY = x * c1r0 + y * c1r1 + z * c1r2 + w * c1r3;
  //Multiply the vector against each part of the 3rd column, then add together
  const resultZ = x * c2r0 + y * c2r1 + z * c2r2 + w * c2r3;
  //Multiply the vector against each part of the 4th column, then add together
  const resultW = x * c3r0 + y * c3r1 + z * c3r2 + w * c3r3;
  return [resultX, resultY, resultZ, resultW];
}
export function multiplyMatrices(matrixA, matrixB) {
  // A faster implementation of this function would not create
  // any new arrays. This creates arrays for code clarity.
  // Slice the second matrix up into rows
  const row0 = [matrixB[0], matrixB[1], matrixB[2], matrixB[3]];
  const row1 = [matrixB[4], matrixB[5], matrixB[6], matrixB[7]];
  const row2 = [matrixB[8], matrixB[9], matrixB[10], matrixB[11]];
  const row3 = [matrixB[12], matrixB[13], matrixB[14], matrixB[15]];
  // Multiply each row by the matrix
  const result0 = multiplyMatrixAndVector(matrixA, row0);
  const result1 = multiplyMatrixAndVector(matrixA, row1);
  const result2 = multiplyMatrixAndVector(matrixA, row2);
  const result3 = multiplyMatrixAndVector(matrixA, row3);
  // Turn the results back into a single matrix
  return [
      result0[0],
      result0[1],
      result0[2],
      result0[3],
      result1[0],
      result1[1],
      result1[2],
      result1[3],
      result2[0],
      result2[1],
      result2[2],
      result2[3],
      result3[0],
      result3[1],
      result3[2],
      result3[3],
  ];
}
export function multiplyArrayOfMatrices(matrices) {
  let inputMatrix = matrices[0];
  for (let m of matrices.slice(1)) {
      inputMatrix = multiplyMatrices(inputMatrix, m);
  }
  return inputMatrix;
}
export function translation(vector) {
  return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0].concat(vector);
}
export function uniformScale(scale) {
  return [scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, 1];
}
