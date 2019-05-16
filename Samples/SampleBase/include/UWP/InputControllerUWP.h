/*     Copyright 2015-2019 Egor Yusov
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#pragma once

#include <memory>
#include <mutex>

namespace Diligent
{
 
class InputControllerUWP
{
public:
    struct ControllerState
    {
        std::mutex mtx;
        MouseState mouseState;
        INPUT_KEY_STATE_FLAGS keySates[static_cast<size_t>(InputKeys::TotalKeys)] = {};
    };

    bool HandleNativeMessage(const void* MsgData){return false;}

    MouseState GetMouseState()
    {
        std::lock_guard<std::mutex> lock(m_State->mtx);
        return m_State->mouseState;
    }

    INPUT_KEY_STATE_FLAGS GetKeyState(InputKeys Key)const
    {
        std::lock_guard<std::mutex> lock(m_State->mtx);
        return m_State->keySates[static_cast<size_t>(Key)];
    }

    std::shared_ptr<ControllerState> GetSharedState()
    {
        return m_State;
    }

private:
    std::shared_ptr<ControllerState> m_State{new ControllerState};
};

}
