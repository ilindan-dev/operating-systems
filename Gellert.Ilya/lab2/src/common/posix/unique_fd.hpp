#pragma once

namespace chat::posix {

/**
 * @brief RAII wrapper for POSIX file descriptors.
 *
 * Guarantees automatic closing of the descriptor (calling close) when
 * the object goes out of scope, preventing resource leaks.
 * Implements unique ownership semantics (similar to std::unique_ptr).
 */
class UniqueFd {
public:
    /**
     * @brief Creates an empty object without an attached descriptor (fd = -1).
     */
    UniqueFd() noexcept = default;

    /**
     * @brief Takes ownership of an existing file descriptor.
     * @param fd Raw POSIX file descriptor.
     */
    explicit UniqueFd(int fd) noexcept;

    /**
     * @brief Closes the descriptor if it is valid (fd >= 0).
     */
    ~UniqueFd();

    /**
     * @brief Move constructor. Takes ownership of the fd from other.
     * @param other The object from which the descriptor is taken (reset to -1).
     */
    UniqueFd(UniqueFd&& other) noexcept;

    /**
     * @brief Move assignment operator.
     * @param other The object from which the descriptor is taken.
     * @return Reference to the current object.
     */
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    // Disable copying to avoid double closing (Double Free)
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    /**
     * @brief Returns the raw descriptor without transferring ownership.
     * @return int The descriptor value.
     */
    [[nodiscard]] int get() const noexcept;

    /**
     * @brief Checks if the object contains a valid descriptor (fd >= 0).
     * @return true If the descriptor is valid.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Releases ownership of the descriptor.
     * The object will no longer call close() on the returned fd.
     * @return int The raw descriptor.
     */
    [[nodiscard]] int release() noexcept;

    /**
     * @brief Closes the current descriptor (if any) and takes ownership of a new one.
     * @param fd New descriptor to own (defaults to -1).
     */
    void reset(int fd = -1) noexcept;

private:
    int fd_ = -1; ///< Stored file descriptor
};

} // namespace chat::posix
