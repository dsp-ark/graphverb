#ifndef SPECTRAL_ANALYZER_H
#define SPECTRAL_ANALYZER_H

#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <vector>

class SpectralAnalyzer {
public:
    /**
     * @brief Constructor for the SpectralAnalyzer.
     *
     * @param fftOrder The FFT order (e.g., 10 for 1024 samples).
     * @param hopSizeIn The hop size (number of samples to advance after each
     * FFT frame). A common default is fftSize/2 for 50% overlap.
     */
    explicit SpectralAnalyzer(const int fftOrder, const int hopSizeIn = -1) :
        fftOrder(fftOrder), fftSize(1 << fftOrder),
        hopSize(hopSizeIn > 0 ? hopSizeIn : (1 << fftOrder) / 2), fifoFill(0),
        fft(fftOrder) {
        /// Initialize the Hann window to reduce spectral leakage.
        window.resize(fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables(
                window.data(), fftSize,
                juce::dsp::WindowingFunction<float>::hann);
        /// Allocate FIFO buffer for the incoming time-domain samples.
        fifoBuffer.resize(fftSize, 0.0f);
        /// Allocate buffer for the FFT result (frequency domain).
        frequencyDomainBuffer.resize(fftSize, 0.0f);
    }

    /**
     * @brief Push new audio samples into the analyzer.
     *
     * This method can be called from your plugin's processBlock() and will
     * handle the accumulation of samples until a full FFT frame is ready.
     *
     * @param input Pointer to incoming audio samples.
     * @param numSamples Number of samples in the input buffer.
     */
    void pushSamples(const float *input, const int numSamples) {
        int index = 0;
        while (index < numSamples) {
            /// Determine how many samples can be copied into the FIFO buffer.
            const int samplesToCopy =
                    std::min(numSamples - index, fftSize - fifoFill);
            std::copy_n(input + index, samplesToCopy,
                        fifoBuffer.begin() + fifoFill);
            fifoFill += samplesToCopy;
            index += samplesToCopy;
            /// If we have filled the FIFO buffer with fftSize samples, process
            /// the frame.
            if (fifoFill == fftSize) {
                processFrame();
                /// Shift the buffer left by hopSize samples to prepare for the
                /// next frame.
                std::copy(fifoBuffer.begin() + hopSize, fifoBuffer.end(),
                          fifoBuffer.begin());
                fifoFill -= hopSize;
            }
        }
    }

    /**
     * @brief Retrieve the magnitudes of the latest FFT frame.
     *
     * @return A const reference to a vector of magnitudes (one per frequency
     * bin).
     */
    [[nodiscard]] const std::vector<float> &getLatestMagnitudes() const {
        return latestMagnitudes;
    }

    /**
     * @brief Reset the analyzer state.
     */
    void reset() {
        fifoFill = 0;
        std::ranges::fill(fifoBuffer, 0.0f);
        std::ranges::fill(frequencyDomainBuffer, 0.0f);
        latestMagnitudes.clear();
    }

private:
    /** FFT order (e.g., 10 for 1024 samples). */
    int fftOrder;

    /** FFT size (number of samples in the FFT frame). */
    int fftSize;

    /** Hop size (number of samples to advance after each FFT frame). */
    int hopSize;

    /** FIFO buffer for incoming audio samples. */
    int fifoFill;

    /** FFT object for performing the FFT. */
    juce::dsp::FFT fft;

    /** Window function to reduce spectral leakage. */
    std::vector<float> window;

    /** Time-domain samples buffer. */
    std::vector<float> fifoBuffer;

    /** Frequency-domain samples buffer. */
    std::vector<float> frequencyDomainBuffer;

    /** Latest magnitudes of the FFT frame. */
    std::vector<float> latestMagnitudes;

    /**
     * @brief Process a full FFT frame using the data in the FIFO buffer.
     *
     * This method applies the window function, performs the FFT, and computes
     * the magnitude for each frequency bin.
     */
    void processFrame() {
        /// Create a temporary frame and apply the window.
        std::vector<float> frame(fifoBuffer.begin(),
                                 fifoBuffer.begin() + fftSize);
        for (int i = 0; i < fftSize; ++i)
            frame[i] *= window[i];
        /// Copy windowed data to the frequency domain buffer.
        std::copy_n(frame.begin(), fftSize, frequencyDomainBuffer.begin());
        /// Perform an in-place FFT. This uses a real-only FFT transform.
        fft.performRealOnlyForwardTransform(frequencyDomainBuffer.data());
        /// Compute magnitudes for the FFT bins.
        latestMagnitudes = computeMagnitudes();
    }

    /**
     * @brief Compute the magnitude spectrum from the FFT result.
     *
     * @return A vector containing the magnitude for each frequency bin.
     */
    [[nodiscard]] std::vector<float> computeMagnitudes() const {
        std::vector<float> magnitudes(fftSize / 2);
        /// DC component (bin 0) is real.
        magnitudes[0] = std::abs(frequencyDomainBuffer[0]);
        /// For bins 1 to N/2 - 1, compute the magnitude from the real and
        /// imaginary parts.
        for (int i = 1; i < fftSize / 2; ++i) {
            const float real = frequencyDomainBuffer[i];
            const float imag = frequencyDomainBuffer[fftSize - i];
            magnitudes[i] = std::sqrt(real * real + imag * imag);
        }
        return magnitudes;
    }
};

#endif // SPECTRAL_ANALYZER_H
