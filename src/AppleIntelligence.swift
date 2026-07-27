import Foundation
import FoundationModels
import Darwin

private final class ResponseBox: @unchecked Sendable {
    var value = "__AETHERCLI_APPLE_AI_ERROR__"
}

@_cdecl("aethercli_apple_intelligence_ask")
public func aethercliAppleIntelligenceAsk(_ instructions: UnsafePointer<CChar>,
                                          _ prompt: UnsafePointer<CChar>) -> UnsafeMutablePointer<CChar>? {
    let instructionText = String(cString: instructions)
    let promptText = String(cString: prompt)
    let result = ResponseBox()
    let semaphore = DispatchSemaphore(value: 0)

    Task {
        defer { semaphore.signal() }
        guard #available(macOS 26.0, *) else {
            result.value = "__AETHERCLI_APPLE_AI_UNAVAILABLE__"
            return
        }
        let model = SystemLanguageModel.default
        guard model.isAvailable else {
            result.value = "__AETHERCLI_APPLE_AI_UNAVAILABLE__"
            return
        }
        do {
            let session = LanguageModelSession(model: model, instructions: instructionText)
            result.value = try await session.respond(to: promptText).content
        } catch {
            result.value = "__AETHERCLI_APPLE_AI_ERROR__"
        }
    }
    if semaphore.wait(timeout: .now() + 60) == .timedOut {
        return strdup("__AETHERCLI_APPLE_AI_TIMEOUT__")
    }
    return strdup(result.value)
}

@_cdecl("aethercli_apple_intelligence_free")
public func aethercliAppleIntelligenceFree(_ response: UnsafeMutablePointer<CChar>?) {
    free(response)
}
