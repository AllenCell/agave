#include "LoadRequest.h"

#include "Logging.h"

#include <chrono>

FutureLoadRequest::FutureLoadRequest(const LoadSpec& spec,
                                     std::shared_ptr<LoadProgress> progress,
                                     std::future<std::shared_ptr<ImageXYZC>> future)
  : LoadRequest(spec, std::move(progress))
  , m_future(std::move(future))
{
}

FutureLoadRequest::~FutureLoadRequest()
{
  if (m_taken || !m_future.valid()) {
    return;
  }
  // Ask the worker to stop, then wait for it. The task writes into buffers it
  // owns, so returning before it finishes would leave a thread running against
  // freed state.
  cancel();
  try {
    m_future.wait();
  } catch (...) {
    // Nothing useful to do while unwinding a destructor.
  }
}

bool
FutureLoadRequest::isReady() const
{
  if (m_taken) {
    return true;
  }
  if (!m_future.valid()) {
    return true;
  }
  return m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::shared_ptr<ImageXYZC>
FutureLoadRequest::take()
{
  if (m_taken) {
    return m_result;
  }
  m_taken = true;
  if (!m_future.valid()) {
    return m_result;
  }
  try {
    m_result = m_future.get();
  } catch (std::exception& e) {
    LOG_ERROR << "Load of " << m_spec.toString() << " failed: " << e.what();
    m_result.reset();
  } catch (...) {
    LOG_ERROR << "Load of " << m_spec.toString() << " failed with an unknown exception";
    m_result.reset();
  }
  return m_result;
}
