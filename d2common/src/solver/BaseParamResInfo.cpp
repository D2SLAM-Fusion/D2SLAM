#include <d2common/solver/BaseParamResInfo.hpp>

namespace D2Common {
ParamInfo createFramePose(D2State * state, FrameIdType id) {
    ParamInfo info;
    info.pointer = state->getPoseState(id);
    info.index = -1;
    info.size = POSE_SIZE;
    info.eff_size = POSE_EFF_SIZE;
    info.type = POSE;
    info.id = id;
    info.data_copied.resize(info.size);
    memcpy(info.data_copied.data(), info.pointer, sizeof(state_type) * info.size);
    return info;
}

void ResidualInfo::Evaluate(D2State * state) {
    auto param_infos = paramsList(state);
    std::vector<state_type*> params;
    for (auto info : param_infos) {
        params.push_back(info.pointer);
    }

    //This function is from VINS.
    residuals.resize(cost_function->num_residuals());
    std::vector<int> blk_sizes = cost_function->parameter_block_sizes();
    std::vector<double *> raw_jacobians(blk_sizes.size());
    jacobians.resize(blk_sizes.size());
    for (int i = 0; i < static_cast<int>(blk_sizes.size()); i++) {
        jacobians[i].resize(cost_function->num_residuals(), blk_sizes[i]);
        jacobians[i].setZero();
        raw_jacobians[i] = jacobians[i].data();
    }
    cost_function->Evaluate(params.data(), residuals.data(), raw_jacobians.data());
    if (loss_function)
    {
        double residual_scaling_, alpha_sq_norm_;

        double sq_norm, rho[3];

        sq_norm = residuals.squaredNorm();
        loss_function->Evaluate(sq_norm, rho);

        double sqrt_rho1_ = sqrt(rho[1]);

        if ((sq_norm == 0.0) || (rho[2] <= 0.0))
        {
            residual_scaling_ = sqrt_rho1_;
            alpha_sq_norm_ = 0.0;
        }
        else
        {
            const double D = 1.0 + 2.0 * sq_norm * rho[2] / rho[1];
            const double alpha = 1.0 - sqrt(D);
            residual_scaling_ = sqrt_rho1_ / (1 - alpha);
            alpha_sq_norm_ = alpha / sq_norm;
        }

        for (int i = 0; i < static_cast<int>(params.size()); i++)
        {
            jacobians[i] = sqrt_rho1_ * (jacobians[i] - alpha_sq_norm_ * residuals * (residuals.transpose() * jacobians[i]));
        }

        residuals *= residual_scaling_;
    }
}
}