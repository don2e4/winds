import React from 'react';
import { createRoot } from 'react-dom/client';
import PixelSwap from '../components/PixelSwap';

function App() {
  return (
    <div className="pixel-swap-wrapper">
      <PixelSwap
        firstContent={
          <div className="swap-box">
            <div className="swap-header">
              <span className="swap-title">C++ Input Source</span>
              <span className="swap-badge">[ Click to pixel-swap to generated x86_64 assembly ]</span>
            </div>
            <pre className="swap-code">
              <code>{`// winds C++ translation unit
int calc(int a, int b) {
    return a * b + 7;
}

int main() {
    return calc(3, 5); // Returns 22
}`}</code>
            </pre>
          </div>
        }
        secondContent={
          <div className="swap-box">
            <div className="swap-header">
              <span className="swap-title">Generated System V AMD64 Assembly (-S)</span>
              <span className="swap-badge">[ Click to pixel-swap to original C++ source ]</span>
            </div>
            <pre className="swap-code">
              <code>{`    .globl  _W_calc
    .type   _W_calc, @function
_W_calc:
    pushq   %rbp
    movq    %rsp, %rbp
    movl    %edi, -4(%rbp)
    movl    %esi, -8(%rbp)
    movl    -4(%rbp), %eax
    imull   -8(%rbp), %eax
    addl    $7, %eax
    movq    %rbp, %rsp
    popq    %rbp
    ret`}</code>
            </pre>
          </div>
        }
        pixelSize={36}
        gap={0}
        pixelRadius={0}
        pixelSpin={0}
        pixelScale={0.3}
        duration={900}
        pixelDuration={350}
        pattern="diagonal"
        randomness={0.1}
        fade={true}
        trigger="click"
        aspectRatio="auto"
      />
    </div>
  );
}

const mountEl = document.getElementById('pixel-swap-root');
if (mountEl) {
  const root = createRoot(mountEl);
  root.render(<App />);
}
