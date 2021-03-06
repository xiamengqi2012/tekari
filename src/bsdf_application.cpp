#include <tekari/bsdf_application.h>

#include <nanogui/layout.h>
#include <nanogui/button.h>
#include <nanogui/toolbutton.h>
#include <nanogui/entypo.h>
#include <nanogui/popupbutton.h>
#include <nanogui/colorwheel.h>
#include <nanogui/checkbox.h>
#include <nanogui/imageview.h>
#include <nanogui/slider.h>
#include <nanogui/combobox.h>
#include <nanogui/vscrollpanel.h>
#include <nanogui/messagedialog.h>
#include <nanogui/label.h>

#include <algorithm>
#include <bitset>
#include <string>
#include <array>

#include <stb_image.h>
#include <tekari/light_theme.h>
#include <tekari/arrow.h>
#include <tekari/bsdf_dataset.h>
#include <tekari/standard_dataset.h>
#include <tekari/wavelength_slider.h>
#include <tekari/graph_spectrum.h>
#include <tekari_resources.h>

#define FOOTER_HEIGHT 25

using nanogui::MessageDialog;
using nanogui::BoxLayout;
using nanogui::GridLayout;
using nanogui::GroupLayout;
using nanogui::Orientation;
using nanogui::ColorWheel;
using nanogui::Alignment;
using nanogui::Theme;
using nanogui::Graph;

TEKARI_NAMESPACE_BEGIN

BSDFApplication::BSDFApplication(const vector<string>& dataset_paths, bool log_mode)
:   Screen(Vector2i(1200, 750), "Tekari", true, false, 8, 8, 24, 8, 2)
,   m_log_mode(log_mode)
,   m_metadata_window(nullptr)
,   m_brdf_options_window(nullptr)
,   m_help_window(nullptr)
,   m_color_map_selection_window(nullptr)
,   m_selection_info_window(nullptr)
,   m_unsaved_data_window(nullptr)
,   m_selected_ds(nullptr)
{
    Arrow::instance().load_shaders();

    // load color maps
    for (auto& p : ColorMap::PREDEFINED_MAPS)
    {
        m_color_maps.push_back(make_shared<ColorMap>(p.first, p.second.first, p.second.second));
    }

    m_3d_view = new Widget{this};
    m_3d_view->set_layout(new BoxLayout{ Orientation::Vertical, Alignment::Fill });

    // canvas
    m_bsdf_canvas = new BSDFCanvas{ m_3d_view };
    m_bsdf_canvas->set_background_color(Color(55, 255));
    m_bsdf_canvas->set_color_map(m_color_maps[0]);
    m_bsdf_canvas->set_selection_callback([this](const Matrix4f& mvp, const SelectionBox& selection_box,
        const Vector2i& canvas_size, SelectionMode mode) {
        if (!m_selected_ds)
            return;

        
        if (selection_box.empty())
        {
            select_closest_point(   m_selected_ds->v2d(),
                                    m_selected_ds->curr_h(),
                                    m_selected_ds->selected_points(),
                                    mvp, selection_box.top_left, canvas_size);
        }
        else
        {
            select_points(  m_selected_ds->v2d(),
                            m_selected_ds->curr_h(),
                            m_selected_ds->selected_points(),
                            mvp, selection_box, canvas_size, mode);
        }
        update_selection_info_window();
    });
    m_bsdf_canvas->set_update_incident_angle_callback([this](const Vector2f& incident_angle) {
        BSDFDataset* bsdf_dataset = dynamic_cast<BSDFDataset*>(m_selected_ds.get());
        if (!bsdf_dataset)
            return;
        
        bsdf_dataset->set_incident_angle(incident_angle);
        if (m_selection_info_window) toggle_selection_info_window();
        reprint_footer();

        if (!m_brdf_options_window)
            return;
        m_theta_float_box->set_value(incident_angle.x());
        m_phi_float_box->set_value(incident_angle.y());
        m_incident_angle_slider->set_value(incident_angle);
    });

    // Footer
    {
        m_footer = new Widget{ m_3d_view };
        m_footer->set_layout(new GridLayout{ Orientation::Horizontal, 3, Alignment::Fill, 5});

        auto make_footer_info = [this](string label) {
            auto container = new Widget{ m_footer };
            container->set_layout(new BoxLayout{ Orientation::Horizontal, Alignment::Fill });
            container->set_fixed_width(width() / 3);
            new Label{ container, label };
            auto info = new Label{ container, "-" };
            return info;
        };

        m_dataset_name = make_footer_info("Material name : ");
        m_dataset_points_count = make_footer_info("Point count : ");
        m_dataset_average_height = make_footer_info("Average value : ");
    }

    m_tool_window = new Window(this, "Tools");
    m_tool_window->set_layout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 5, 5});
    m_tool_window->set_visible(true);
    m_tool_window->set_position({ 20, 20 });

    m_help_button = new Button(m_tool_window->button_panel(), "", ENTYPO_ICON_HELP);
    m_help_button->set_callback([this]() { toggle_help_window(); });
    m_help_button->set_font_size(15);
    m_help_button->set_tooltip("Information about using Tekari (H)");
    m_help_button->set_position({20, 0});

    // Hidden options
    {
        m_hidden_options_button = new PopupButton(m_tool_window->button_panel(), "", ENTYPO_ICON_TOOLS);
        m_hidden_options_button->set_background_color(Color{0.4f, 0.1f, 0.1f, 1.0f});
        m_hidden_options_button->set_tooltip("Additional view options");
        auto hidden_options_popup = m_hidden_options_button->popup();
        hidden_options_popup->set_layout(new GroupLayout{});

        auto add_hidden_option_toggle = [hidden_options_popup](const string& label, const string& tooltip,
            const function<void(bool)>& callback, bool checked = false) {
            auto checkbox = new CheckBox{ hidden_options_popup, label, callback };
            checkbox->set_checked(checked);
            checkbox->set_tooltip(tooltip);
            return checkbox;
        };

        new Label{ hidden_options_popup, "Advanced View Options", "sans-bold" };

        m_ortho_view_checkbox = add_hidden_option_toggle("Orthographic", "Enable/Disable orthographic view (O)",
            [this](bool checked) {
                m_bsdf_canvas->set_ortho_mode(checked);
        }, false);
        m_use_diffuse_shading_checkbox = add_hidden_option_toggle("Diffuse shading", "Enable/Disable diffuse shading (S)",
            [this](bool checked) {
            m_bsdf_canvas->set_draw_flag(USE_SHADOWS, checked);
            m_use_specular_shading_checkbox->set_enabled(checked);
        }, true);
        m_use_specular_shading_checkbox = add_hidden_option_toggle("Specular shading", "Enable/Disable specular shading (Shift+S)",
            [this](bool checked) {
            m_bsdf_canvas->set_draw_flag(USE_SPECULAR, checked);
        }, false);
#if !defined(EMSCRIPTEN)
        m_use_wireframe_checkbox = add_hidden_option_toggle("Wireframe", "Enable/Disable wireframe (W)",
            [this](bool checked) {
            m_bsdf_canvas->set_draw_flag(USE_WIREFRAME, checked);
        }, false);
#endif
        m_display_center_axis = add_hidden_option_toggle("Center axis", "Show/hide center axis (A)",
            [this](bool checked) {
            m_bsdf_canvas->set_draw_flag(DISPLAY_AXIS, checked);
        }, true);
        m_display_predicted_outgoing_angle_checkbox = add_hidden_option_toggle("Predicted outgoing angle", "Show/hide predicted outgoing angle (Ctrl+I)",
            [this](bool checked) {
            m_bsdf_canvas->set_draw_flag(DISPLAY_PREDICTED_OUTGOING_ANGLE, checked);
        });
        add_hidden_option_toggle("Use light theme", "Switch between dark and light themes",
            [this](bool checked) {
            set_theme(checked ? new LightTheme{ nvg_context() } : new Theme{ nvg_context() });
            set_background(m_theme->m_window_fill_focused);
            m_hidden_options_button->set_background_color(checked ? Color{ 0.7f, 0.3f, 0.3f, 1.0f } : Color{ 0.4f, 0.1f, 0.1f, 1.0f });
        });

        auto point_size_label = new Label{ hidden_options_popup , "Point size" };
        point_size_label->set_tooltip("Changes the size used to render the point mode visualization");
        auto point_size_slider = new Slider{ hidden_options_popup };
        point_size_slider->set_range(make_pair(0.1f, 20.0f));
        point_size_slider->set_value(m_bsdf_canvas->point_size_scale());
        point_size_slider->set_callback([this](float value) {
            m_bsdf_canvas->set_point_size_scale(value);
        });

        auto chose_color_map_button = new Button{ hidden_options_popup, "Choose color map" };
        chose_color_map_button->set_tooltip("Choose with which color map the data should be displayed (Shift+M)");
        chose_color_map_button->set_callback([this]() {
            toggle_color_map_selection_window();
        });

        new Label{ hidden_options_popup, "Grid Options", "sans-bold" };

        m_grid_view_checkbox = add_hidden_option_toggle("Grid", "Show/hide radial grid (G)",
            [this](bool checked) {
            m_bsdf_canvas->grid().set_visible(checked);
            m_display_degrees_checkbox->set_enabled(checked);
        }, true);
        m_display_degrees_checkbox = add_hidden_option_toggle("Grid angles", "Show/hide degree values on grid (Shift+G)",
            [this](bool checked) { m_bsdf_canvas->grid().set_show_degrees(checked); }, true);

        auto grid_color_label = new Label{ hidden_options_popup, "Color" };
        grid_color_label->set_tooltip("Choose the color of the angular grid");
        auto colorwheel = new ColorWheel{ hidden_options_popup, m_bsdf_canvas->grid().color() };

        auto grid_alpha_label = new Label{ hidden_options_popup, "Opacity" };
        grid_alpha_label->set_tooltip("Set the angular grid opacity (left = invisible, right = opaque)");
        auto grid_alpha_slider = new Slider{ hidden_options_popup };
        grid_alpha_slider->set_range({ 0.0f, 1.0f });
        grid_alpha_slider->set_callback([this](float value) { m_bsdf_canvas->grid().set_alpha(value); });

        grid_alpha_slider->set_value(m_bsdf_canvas->grid().alpha());

        colorwheel->set_callback([grid_alpha_slider, this](const Color& value) {
            m_bsdf_canvas->grid().set_color(value);
            m_bsdf_canvas->grid().set_alpha(grid_alpha_slider->value());
        });

        // auto background_color_popup_button = new PopupButton(panel, "", ENTYPO_ICON_BUCKET);

        // background_color_popup_button->set_font_size(15);
        // background_color_popup_button->set_chevron_icon(0);
        // background_color_popup_button->set_tooltip("Background Color");

        // // Background color popup
        // {
        //     auto popup = background_color_popup_button->popup();
        //     popup->set_layout(new BoxLayout{Orientation::Vertical, Alignment::Fill, 10});

        //     new Label{popup, "Background Color"};
        //     auto colorwheel = new ColorWheel{popup, m_bsdf_canvas->background_color()};

        //     colorwheel->set_callback([this](const Color& value) { m_bsdf_canvas->set_background_color(value); });
        // }
    }


    // mouse mode
    {
        using MouseMode = BSDFCanvas::MouseMode;

        auto mouse_mode_container = new Widget{ m_tool_window };
        mouse_mode_container->set_layout(new GridLayout{ Orientation::Horizontal, 2, Alignment::Fill, 0, 10 });

        auto mouse_mode_label = new Label{ mouse_mode_container, "Mouse mode: ", "sans-bold"};
        mouse_mode_label->set_tooltip("Change mouse mode to rotation (R), translation (T) or box selection (B)");

        auto mouse_mode_buttons_container = new Widget{ mouse_mode_container };
        mouse_mode_buttons_container->set_layout(new BoxLayout{ Orientation::Horizontal, Alignment::Fill, 0, 6 });

        int mouse_mode_icons[MouseMode::MOUSE_MODE_COUNT] = { ENTYPO_ICON_CW, nvg_image_icon(m_nvg_context, translate_cross), ENTYPO_ICON_DOCUMENT_LANDSCAPE };
        int cursors_ids[MouseMode::MOUSE_MODE_COUNT] = { GLFW_ARROW_CURSOR, GLFW_HAND_CURSOR, GLFW_CROSSHAIR_CURSOR };

        for (int i = 0; i != static_cast<int>(MouseMode::MOUSE_MODE_COUNT); ++i)
        {
            MouseMode mode = static_cast<MouseMode>(i);
            m_cursors[mode] = glfwCreateStandardCursor(cursors_ids[mode]);
            m_mouse_mode_buttons[mode] = new Button{ mouse_mode_buttons_container, "", mouse_mode_icons[mode] };
            m_mouse_mode_buttons[mode]->set_flags(Button::Flags::ToggleButton);
            m_mouse_mode_buttons[mode]->set_font_size(20);
            m_mouse_mode_buttons[mode]->set_fixed_size(Vector2i(32, 26));
            m_mouse_mode_buttons[mode]->set_pushed(mode == m_bsdf_canvas->mouse_mode());
            m_mouse_mode_buttons[mode]->set_callback([this, mode]() {
                for (int i = 0; i != static_cast<int>(MouseMode::MOUSE_MODE_COUNT); ++i)
                {
                    MouseMode m = static_cast<MouseMode>(i);
                    m_mouse_mode_buttons[m]->set_pushed(mode == m);
                }
                m_bsdf_canvas->set_mouse_mode(mode);
                glfwSetCursor(m_glfw_window, m_cursors[mode]);          
            });
        }

        glfwSetCursor(m_glfw_window, m_cursors[m_bsdf_canvas->mouse_mode()]);
    }

    // Open, save screenshot, save data
    {
        new Label{ m_tool_window, "Loaded materials", "sans-bold" };
        auto tools = new Widget{ m_tool_window };
        tools->set_layout(new GridLayout{Orientation::Horizontal, 4, Alignment::Fill});

        auto make_tool_button = [&](bool enabled, function<void()> callback, int icon = 0, string tooltip = "") {
            auto button = new Button{tools, "", icon};
            button->set_callback(callback);
            button->set_tooltip(tooltip);
            button->set_font_size(20);
            button->set_enabled(enabled);
            return button;
        };

        auto open_button        = make_tool_button(true, [this] { open_dataset_dialog(); }, ENTYPO_ICON_FOLDER, "Open dataset (CTRL+O)");
        auto save_image_button  = make_tool_button(true, [this] { save_screen_shot(); }, ENTYPO_ICON_IMAGE, "Save image (CTRL+P)");
        auto save_data_button   = make_tool_button(true, [this] { save_selected_dataset(); }, ENTYPO_ICON_SAVE, "Save data (CTRL+S)");
        auto show_infos_button  = make_tool_button(true, [this]() { toggle_metadata_window(); }, ENTYPO_ICON_INFO, "Show selected dataset infos (I)");

#if defined(EMSCRIPTEN)
        open_button->set_enabled(false);
        save_image_button->set_enabled(false);
        save_data_button->set_enabled(false);
#else
        (void)open_button;
        (void)save_image_button;
        (void)save_data_button;
#endif
        (void)show_infos_button;
    }

    // Dataset selection
    {
        m_datasets_scroll_panel = new VScrollPanel{ m_tool_window };

        m_scroll_content = new Widget{ m_datasets_scroll_panel };
        m_scroll_content->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill));

        m_dataset_button_container = new Widget{ m_scroll_content };
        m_dataset_button_container->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 0, 0));
    }

#if !defined(EMSCRIPTEN)
    // load application icon
    {
        const std::vector<pair<const uint8_t*, uint32_t>> icon_paths =
        {
            { tekari_icon_16x16_png, tekari_icon_16x16_png_size },
            { tekari_icon_32x32_png, tekari_icon_32x32_png_size },
            { tekari_icon_64x64_png, tekari_icon_64x64_png_size },
            { tekari_icon_128x128_png, tekari_icon_128x128_png_size },
            { tekari_icon_256x256_png, tekari_icon_256x256_png_size }
        };

        GLFWimage icons[10];
        size_t i;
        for (i = 0; i < icon_paths.size(); i++)
        {
            int num_chanels;
            icons[i].pixels = stbi_load_from_memory(icon_paths[i].first, icon_paths[i].second,
                                                    &icons[i].width, &icons[i].height, &num_chanels, 0);
            if (!icons[i].pixels)
            {
                cout << "Warning : unable to load Tekari's icons\n";
                break;
            }
        }
        if (i == icon_paths.size())
            glfwSetWindowIcon(m_glfw_window, (int) icon_paths.size(), icons);

        for (size_t j = 0; j < i; j++)
        {
            stbi_image_free(icons[j].pixels);
        }
    }
#endif

    set_resize_callback([this](Vector2i) { request_layout_update(); });
    set_background(m_theme->m_window_fill_focused);

    request_layout_update();
    open_files(dataset_paths);
}

BSDFApplication::~BSDFApplication()
{
    m_framebuffer.free();

    for (size_t i = 0; i < BSDFCanvas::MOUSE_MODE_COUNT; i++)
    {
        glfwDestroyCursor(m_cursors[i]);
    }
}

bool BSDFApplication::keyboard_event(int key, int scancode, int action, int modifiers) {
    if (Screen::keyboard_event(key, scancode, action, modifiers))
        return true;

    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        bool alt = modifiers & GLFW_MOD_ALT;
        // control options
        if (modifiers & SYSTEM_COMMAND_MOD)
        {
            switch (key)
            {
#if !defined(EMSCRIPTEN)
            case GLFW_KEY_O:
                open_dataset_dialog();
                return true;
            case GLFW_KEY_S:
                save_selected_dataset();
                return true;
            case GLFW_KEY_P:
                save_screen_shot();
                return true;
#endif
            case GLFW_KEY_A:
                if (!m_selected_ds)
                    return false;
                select_all_points(m_selected_ds->selected_points());
                update_selection_info_window();
                return true;
            case GLFW_KEY_1: if (!alt) return false;
            case GLFW_KEY_KP_1:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::BACK);
                return true;
            case GLFW_KEY_3: if (!alt) return false;
            case GLFW_KEY_KP_3:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::RIGHT);
                return true;
            case GLFW_KEY_7: if (!alt) return false;
            case GLFW_KEY_KP_7:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::DOWN);
                return true;
            case GLFW_KEY_I:
                toggle_tool_checkbox(m_display_predicted_outgoing_angle_checkbox);
                return true;
            }
        }
        else if (modifiers & GLFW_MOD_SHIFT)
        {
            switch (key)
            {
                case GLFW_KEY_S:
                    toggle_tool_checkbox(m_use_specular_shading_checkbox);
                    return true;
                case GLFW_KEY_M:
                    toggle_color_map_selection_window();
                    return true;
                case GLFW_KEY_G:
                {
                    int show_degrees = !m_bsdf_canvas->grid().show_degrees();
                    m_display_degrees_checkbox->set_checked(show_degrees);
                    m_bsdf_canvas->grid().set_show_degrees(show_degrees);
                    return true;
                }
                case GLFW_KEY_P:
                    toggle_view(Dataset::Views::PATH);
                    return true;
                case GLFW_KEY_I:
                    toggle_view(Dataset::Views::INCIDENT_ANGLE);
                    return true;
                case GLFW_KEY_H:
                case GLFW_KEY_L:
                    if (!m_selected_ds)
                        return false;
                    
                    select_extreme_point(   m_selected_ds->points_stats(),
                                            m_selected_ds->selection_stats(),
                                            m_selected_ds->selected_points(), 
                                            m_selected_ds->intensity_index(),
                                            key == GLFW_KEY_H);

                    update_selection_info_window();
                    return true;
                case GLFW_KEY_1:
                case GLFW_KEY_2:
                    if (!m_selected_ds || !m_selected_ds->has_selection())
                        return false;

                    move_selection_along_path(key == GLFW_KEY_1, m_selected_ds->selected_points());
                    update_selection_info_window();
                    return true;
                default:
                    return false;
            }
        }
        else if (alt)
        {
            switch (key)
            {
            case GLFW_KEY_1:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::FRONT);
                return true;
            case GLFW_KEY_3:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::LEFT);
                return true;
            case GLFW_KEY_7:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::UP);
                return true;
            }
        }
        else
        {
            switch (key)
            {
            case GLFW_KEY_F1:
                hide_windows();
                return true;
            case GLFW_KEY_ESCAPE:
                if (!m_selected_ds || !m_selected_ds->has_selection())
                    return false;
                 
                deselect_all_points(m_selected_ds->selected_points());
                update_selection_info_window();
                return true;
#if !defined(EMSCRIPTEN)
            case GLFW_KEY_Q:
            {
                vector<string> ds_names;
                for (const auto& ds : m_datasets)
                    if (ds->dirty())
                        ds_names.push_back(ds->name());
                
                if (ds_names.empty()) set_visible(false);
                else toggle_unsaved_data_window(ds_names, [this]() { set_visible(false); });
            }
                return true;
#endif
            case GLFW_KEY_1: case GLFW_KEY_2: case GLFW_KEY_3: case GLFW_KEY_4: case GLFW_KEY_5:
            case GLFW_KEY_6: case GLFW_KEY_7: case GLFW_KEY_8: case GLFW_KEY_9:
                select_dataset(key - GLFW_KEY_1);
                return true;
            case GLFW_KEY_DELETE:
                delete_dataset(m_selected_ds);
                return true;
            case GLFW_KEY_D:
                if (!m_selected_ds || !m_selected_ds->has_selection())
                    return false;

                m_selected_ds->delete_selected_points();
                m_selected_ds->set_dirty(true);
                corresponding_button(m_selected_ds)->set_dirty(true);

                reprint_footer();
                update_selection_info_window();
                request_layout_update();
                return true;
            case GLFW_KEY_W:
                toggle_tool_checkbox(m_use_wireframe_checkbox);
                return true;
            case GLFW_KEY_S:
                toggle_tool_checkbox(m_use_diffuse_shading_checkbox);
                return true;
            case GLFW_KEY_UP:
                select_dataset(selected_dataset_index() - 1, false);
                return true;
            case GLFW_KEY_DOWN:
                select_dataset(selected_dataset_index() + 1, false);
                return true;
            case GLFW_KEY_ENTER:
                if (!m_selected_ds)
                    return false;
                corresponding_button(m_selected_ds)->toggle_view();
                return true;
            case GLFW_KEY_L:
                if (!m_selected_ds)
                    return false;

                m_selected_ds->toggle_log_view();
                if (m_brdf_options_window)
                    m_display_as_log->set_pushed(m_selected_ds->display_as_log());
                return true;
            case GLFW_KEY_T: case GLFW_KEY_R: case GLFW_KEY_B:
            {
                using MouseMode = BSDFCanvas::MouseMode;
                MouseMode mode = MouseMode::ROTATE;
                if (key == GLFW_KEY_T) mode = MouseMode::TRANSLATE;
                if (key == GLFW_KEY_B) mode = MouseMode::SELECT;
                m_mouse_mode_buttons[mode]->callback()();
                return true;
            }
            case GLFW_KEY_P:
                toggle_view(Dataset::Views::POINTS);
                return true;
            case GLFW_KEY_G:
                toggle_tool_checkbox(m_grid_view_checkbox);
                return true;
            case GLFW_KEY_O: case GLFW_KEY_KP_5:
                toggle_tool_checkbox(m_ortho_view_checkbox);
                return true;
            case GLFW_KEY_I:
                toggle_metadata_window();
                return true;
            case GLFW_KEY_C:
                m_bsdf_canvas->snap_to_selection_center();
                return true;
            case GLFW_KEY_M:
                toggle_view(Dataset::Views::MESH);
                return true;
            case GLFW_KEY_A:
                toggle_tool_checkbox(m_display_center_axis);
                return true;
            case GLFW_KEY_H:
                toggle_help_window();
                return true;
            case GLFW_KEY_KP_1:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::FRONT);
                return true;
            case GLFW_KEY_KP_3:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::LEFT);
                return true;
            case GLFW_KEY_KP_7:
                m_bsdf_canvas->set_view_angle(BSDFCanvas::ViewAngles::UP);
                return true;
            case GLFW_KEY_U:
                toggle_brdf_options_window();
                return true;
            case GLFW_KEY_KP_ADD:
            case GLFW_KEY_KP_SUBTRACT:
                if (!m_selected_ds || !m_selected_ds->has_selection())
                    return false;

                move_selection_along_path(key == GLFW_KEY_KP_ADD, m_selected_ds->selected_points());
                update_selection_info_window();
                return true;
            default:
                return false;
            }
        }
    }
    return false;
}

void BSDFApplication::draw_contents() {
    if (m_requires_layout_update)
    {
        update_layout();
        m_requires_layout_update = false;
    }

    auto open_error_window = [this](const string& error_msg) {
        auto error_msg_dialog = new MessageDialog(this, MessageDialog::Type::Warning, "Error",
            error_msg, "Retry", "Cancel", true);
        error_msg_dialog->set_callback([this](int index) {
            if (index == 0) { open_dataset_dialog(); }
        });
    };

    try {
        while (true) {
            auto new_dataset = m_datasets_to_add.try_pop();
            if (!new_dataset->dataset)
            {
               open_error_window(new_dataset->error_msg);
            }
            else
            {
                bool init_completed = false;
                try {
                    init_completed = new_dataset->dataset->init();
                } catch (std::runtime_error) {
                    init_completed = false;
                }
                if (!init_completed)
                    open_error_window("Error while initializing the dataset");
                else {
                    add_dataset(new_dataset->dataset);
                    if (m_log_mode) {
                        new_dataset->dataset->toggle_log_view();
                        if (m_brdf_options_window)
                            m_display_as_log->set_pushed(m_selected_ds->display_as_log());
                    }
                }
            }
            redraw();
        }
    }
    catch (std::runtime_error) {
    }
}

void BSDFApplication::update_layout()
{
    m_3d_view->set_fixed_size(m_size);

    m_footer->set_fixed_size(Vector2i( m_size.x(), FOOTER_HEIGHT ));
    for(auto& footer_infos: m_footer->children())
        footer_infos->set_fixed_width(width() / (int) m_footer->children().size());

    m_bsdf_canvas->set_fixed_size(Vector2i{ m_size.x(), m_size.y() - FOOTER_HEIGHT });
    m_tool_window->set_fixed_size({ 210, 400 });

    m_datasets_scroll_panel->set_fixed_height(
        m_tool_window->height() - m_datasets_scroll_panel->position().y()
    );

    perform_layout();

    if (m_brdf_options_window)
        m_brdf_options_window->set_position(m_size - m_brdf_options_window->size() - Vector2i{ 10, 40 });

    // With a changed layout the relative position of the mouse
    // within children changes and therefore should get updated.
    // nanogui does not handle this for us.
    double x, y;
    glfwGetCursorPos(m_glfw_window,& x,& y);
    cursor_pos_callback_event(x, y);
}

void BSDFApplication::open_dataset_dialog()
{
    vector<string> dataset_paths = nanogui::file_dialog(
        {
            { "txt",  "Datasets" },
            { "bsdf",  "Datasets" }
        }, false, true);
    open_files(dataset_paths);
    // Make sure we gain focus after seleting a file to be loaded.
    glfwFocusWindow(m_glfw_window);
}

void BSDFApplication::open_files(const vector<string>& dataset_paths)
{
    for (const auto& dataset_path : dataset_paths)
    {
        m_thread_pool.add_task([this, dataset_path]() {
            auto new_dataset = make_shared<Dataset_to_add>();
            try_load_dataset(dataset_path, new_dataset);
            m_datasets_to_add.push(new_dataset);
            redraw();
        });
    }
}

void BSDFApplication::save_selected_dataset()
{
    if (!m_selected_ds)
        return;
    
    string path = nanogui::file_dialog(
    {
        { "txt",  "Datasets" },
    }, true);

    if (path.empty())
        return;

    m_selected_ds->save(path);
    m_selected_ds->set_dirty(false);
    corresponding_button(m_selected_ds)->set_dirty(false);
}

void BSDFApplication::save_screen_shot()
{
    string screenshot_name = nanogui::file_dialog(
    {
        { "tga", "TGA images" }
    }, true);

    if (screenshot_name.empty())
        return;
        
    if (m_framebuffer.ready())
    {
        m_framebuffer.free();
    }
    int view_port_width = static_cast<int>(m_pixel_ratio* width());
    int view_port_height = static_cast<int>(m_pixel_ratio* height());
    m_framebuffer.init(Vector2i{ view_port_width, view_port_height }, 1);
    glViewport(0, 0, view_port_width, view_port_height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    m_framebuffer.bind();
    draw_all();
    m_framebuffer.download_tga(screenshot_name);
    m_framebuffer.release();
}

void BSDFApplication::toggle_window(Window* & window, function<Window*(void)> create_window)
{
    if (window)
    {
        window->dispose();
        window = nullptr;
    }
    else
    {
        window = create_window();
        window->request_focus();
        window->set_visible(!m_distraction_free_mode);
        request_layout_update();
    }
}

void BSDFApplication::toggle_metadata_window()
{
    toggle_window(m_metadata_window, [this]() {
        Window* window;
        if (m_selected_ds)
        {
            window = new MetadataWindow(this, &m_selected_ds->metadata(), [this]() { toggle_metadata_window(); });
        }
        else
        {
            auto error_window = new MessageDialog{ this, MessageDialog::Type::Warning, "Metadata",
                "No dataset selected.", "close" };
            error_window->set_callback([this](int) { m_metadata_window = nullptr; });
            window = error_window;
        }
        window->center();
        return window;
    });
}

void BSDFApplication::toggle_brdf_options_window()
{
    toggle_window(m_brdf_options_window, [this]() -> Window* {
        Window *window = new Window(this, "BRDF Parameters");
        window->set_layout(new BoxLayout(Orientation::Vertical, Alignment::Fill, 5, 5));

        auto close_button = new Button{ window->button_panel(), "", ENTYPO_ICON_CROSS };
        close_button->set_callback([this]() { toggle_brdf_options_window(); });


        // view modes
        {
            new Label{ window, "View options" , "sans-bold", 18};

            auto button_container = new Widget{ window };
            button_container->set_layout(new BoxLayout{ Orientation::Horizontal, Alignment::Fill, 0, 1 });

            m_display_as_log = new Button{ button_container, "", nvg_image_icon(m_nvg_context, log) };
            m_display_as_log->set_flags(Button::Flags::ToggleButton);
            m_display_as_log->set_tooltip("Logarithmic scale (L)");
            m_display_as_log->set_font_size(20);
            m_display_as_log->set_pushed(m_selected_ds ? false : m_selected_ds->display_as_log());
            m_display_as_log->set_enabled(m_selected_ds != nullptr);
            m_display_as_log->set_change_callback([this](bool /* unused*/) { m_selected_ds->toggle_log_view(); });

            auto make_view_button = [this, button_container](int icon, const string& tooltip, Dataset::Views view) {
                auto button = new Button(button_container, "", icon);
                button->set_flags(Button::Flags::ToggleButton);
                button->set_tooltip(tooltip);
                button->set_font_size(20);
                button->set_pushed(m_selected_ds && m_selected_ds->display_view(view));
                button->set_enabled(m_selected_ds != nullptr);
                button->set_change_callback([this, view](bool) { toggle_view(view); });
                return button;
            };
            m_view_toggles[Dataset::Views::MESH]   = make_view_button(nvg_image_icon(m_nvg_context, mesh), "Show/hide mesh for this material (M)", Dataset::Views::MESH);
            m_view_toggles[Dataset::Views::POINTS] = make_view_button(nvg_image_icon(m_nvg_context, points), "Show/hide sample points for this material (P)", Dataset::Views::POINTS);
            m_view_toggles[Dataset::Views::PATH]   = make_view_button(nvg_image_icon(m_nvg_context, path), "Show/hide measurement path for this material (Shift+P)", Dataset::Views::PATH);
            m_view_toggles[Dataset::Views::INCIDENT_ANGLE] = make_view_button(nvg_image_icon(m_nvg_context, incident_angle), "Show/hide incident angle for this material (Shift+I)", Dataset::Views::INCIDENT_ANGLE);
        }

        // resolution
        BSDFDataset* bsdf_dataset = dynamic_cast<BSDFDataset*>(m_selected_ds.get());
        if (bsdf_dataset)
        {
            new Label{ window, "Sampling resolution", "sans-bold", 18 };
            auto resolution_combobox = new ComboBox{ window };

            pair<size_t, size_t> sampling_resolution = bsdf_dataset->sampling_resolution();
            int current_resolution_index = log2(sampling_resolution.first / 16);

            resolution_combobox->set_items({ "16x16", "32x32", "64x64", "128x128", "256x256" });
            resolution_combobox->set_selected_index(current_resolution_index);
            resolution_combobox->set_side(nanogui::Popup::Side::Left);
            resolution_combobox->set_callback([bsdf_dataset, this](int index) {
                int n = 16 * pow(2, index);
                bsdf_dataset->set_sampling_resolution(n, n);
                reprint_footer();
            });
            resolution_combobox->set_tooltip("Change sampling resolution used to render the BSDF data");
        }

        // incident angle
        {
            Vector2f curr_i_angle = m_selected_ds->incident_angle();
            if (curr_i_angle[1] > 180.0f) curr_i_angle[1] -= 360.0f;

            new Label{ window, "Incident angle", "sans-bold", 18 };
            m_incident_angle_slider = new Slider2D{ window };
            m_incident_angle_slider->set_value(curr_i_angle);
            m_incident_angle_slider->set_callback([this, bsdf_dataset](Vector2f value) {
                m_theta_float_box->set_value(value[0]);
                m_phi_float_box->set_value(value[1]);

                bsdf_dataset->set_incident_angle(value);
                if (m_selection_info_window) toggle_selection_info_window();
                reprint_footer();
            });
            m_incident_angle_slider->set_range(make_pair(Vector2f(0.0f, -180.0f), Vector2f(85.0f, 180.0f)));
            m_incident_angle_slider->set_fixed_size({ 200, 200 });
            m_incident_angle_slider->set_enabled(bsdf_dataset != nullptr);
            m_incident_angle_slider->set_tooltip("Incident angle (can only be changed if a full BRDF was acquired)");

            auto add_float_box = [window, bsdf_dataset](const string& label, float value, function<void(float)> callback) {
                auto float_box_container = new Widget{window};
                float_box_container->set_layout(new GridLayout{Orientation::Horizontal, 2, Alignment::Fill});
                new Label{float_box_container, label};
                auto float_box = new FloatBox<float>{ float_box_container };
                float_box->set_value(value);
                float_box->set_editable(bsdf_dataset != nullptr);
                float_box->set_enabled(bsdf_dataset != nullptr);
                float_box->set_callback(callback);
                float_box->set_units("°");
                float_box->set_spinnable(true);
                return float_box;
            };

            auto angle_slider_callback = [this, bsdf_dataset](float) {
                if (!bsdf_dataset)
                    return;
                float theta = enoki::clamp(m_theta_float_box->value(), 0.0f, 85.0f);
                float phi = enoki::clamp(m_phi_float_box->value(), -180.0f, 180.0f);
                m_theta_float_box->set_value(theta);
                m_phi_float_box->set_value(phi);
                Vector2f incident_angle = {theta, phi};
                m_incident_angle_slider->set_value(incident_angle);
                bsdf_dataset->set_incident_angle(incident_angle); 
                if (m_selection_info_window) toggle_selection_info_window();
                reprint_footer();
            };

            m_theta_float_box = add_float_box("Elevation:", curr_i_angle.x(), angle_slider_callback);
            m_phi_float_box = add_float_box("Azimuth:", curr_i_angle.y(), angle_slider_callback);
        }

        // wavelength slider
        {
            auto add_text = [window](const string& label, const string& value) {
                auto labels_container = new Widget{window};
                labels_container->set_layout(new GridLayout{Orientation::Horizontal, 2, Alignment::Fill});
                new Label{ labels_container, label, "sans-bold", 18 };
                auto text = new Label{ labels_container, value };
                return text;
            };

            size_t wavelength_index = m_selected_ds ? m_selected_ds->intensity_index() : 0;
            float slider_value = m_selected_ds ? float(wavelength_index) / m_selected_ds->intensity_count() : 0;
            string wavelength_str = m_selected_ds ? m_selected_ds->wavelength_str() : "0 nm";
            if (m_selected_ds->wavelengths().size() > 1) {
                auto wavelength_label = add_text("Wavelength:", wavelength_str);

                auto wavelength_slider = new WavelengthSlider{ window, m_selected_ds->wavelengths(), m_selected_ds->wavelengths_colors() };
                wavelength_slider->set_callback([this, wavelength_label, wavelength_slider](float /*unused*/) {
                    int wavelength_index = wavelength_slider->wavelength_index();
                    m_selected_ds->set_intensity_index(wavelength_index);
                    wavelength_label->set_caption(m_selected_ds->wavelength_str());
                    reprint_footer();
                });
                wavelength_slider->set_enabled(m_selected_ds != nullptr);
                wavelength_slider->set_value(slider_value);
                wavelength_slider->set_tooltip("Current displayed wavelength");
            }
        }

        return window;
    });
}

void BSDFApplication::update_selection_info_window()
{
    if (m_selected_ds) m_selected_ds->update_point_selection();
    if(m_selection_info_window) toggle_selection_info_window();
    toggle_selection_info_window();
}

void BSDFApplication::toggle_selection_info_window()
{
    // if we are trying to toggle the selection window without a selection, just return
    if (!m_selection_info_window &&
        (!m_selected_ds || !m_selected_ds->has_selection()))
        return;

    toggle_window(m_selection_info_window, [this]() {

        auto window = new Window{ this, "Selection Info" };
        window->set_layout(new BoxLayout{ Orientation::Vertical, Alignment::Fill, 5, 5 });

        auto labels_container = new Widget{ window };
        labels_container->set_layout(new GridLayout{ Orientation::Horizontal, 2, Alignment::Fill, 0, 5});

        auto make_selection_info_labels = [labels_container](const string& caption, const string& value) {
            new Label{ labels_container, caption, "sans-bold" };
            new Label{ labels_container, value };
        };

        size_t points_count = m_selected_ds->selection_stats().points_count;
        const PointsStats::Slice& selection_stats_slice = m_selected_ds->curr_selection_stats();
        make_selection_info_labels("Points in selection :", to_string(points_count));
        make_selection_info_labels("Minimum intensity :", to_string(selection_stats_slice.min_intensity));
        make_selection_info_labels("Maximum intensity :", to_string(selection_stats_slice.max_intensity));
        make_selection_info_labels("Average intensity :", to_string(selection_stats_slice.average_intensity));

        if (m_selected_ds->wavelengths().size() > 1) {
            new Label{ window, "Spectral plot", "sans-bold" };
            auto graph = new GraphSpectrum{ window, m_selected_ds->wavelengths_colors(), "" };
            m_selected_ds->get_selection_spectrum(graph->values());
            graph->set_stroke_color(Color(.8f, 1.f));
            graph->set_fill_color(Color(0.f, 0.f));
            graph->set_background_color(Color(0.0f, 0.0f));
            graph->set_tooltip("Spectrum of the highest selected point");

            auto wavelength_range_labels_container = new Widget{ window };
            wavelength_range_labels_container->set_layout(new BoxLayout{Orientation::Horizontal, Alignment::Fill, 0, 110});
            auto wavelength_360 = new Label{ wavelength_range_labels_container, "360 nm"  };
            auto wavelength_1000 = new Label{ wavelength_range_labels_container, "1000 nm" };
            wavelength_360->set_font_size(13);
            wavelength_1000->set_font_size(13);
        }

        window->set_position(Vector2i{width() - 210, 20});
        return window;
    });
}

void BSDFApplication::toggle_unsaved_data_window(const vector<string>& dataset_names, function<void(void)> continue_callback)
{
    if (dataset_names.empty())
        return;

    toggle_window(m_unsaved_data_window, [this,& dataset_names, continue_callback]() {
        std::ostringstream error_msg;
        error_msg << dataset_names[0];
        for (size_t i = 1; i < dataset_names.size(); ++i)
            error_msg << " and " << dataset_names[i];

        error_msg << (dataset_names.size() == 1 ? " has " : " have ");
        error_msg << "some unsaved changed. Are you sure you want to continue ?";

        auto window = new MessageDialog{ this, MessageDialog::Type::Warning, "Unsaved Changes",
            error_msg.str(), "Cancel", "Continue", true };

        window->set_callback([this, continue_callback](int i) {
            if (i != 0)
                continue_callback();
            m_unsaved_data_window = nullptr;
        });
        window->center();
        return window;
    });
}

void BSDFApplication::toggle_help_window()
{
    toggle_window(m_help_window, [this]() {
        auto window = new HelpWindow(this, [this]() {toggle_help_window(); });
        window->center();
        return window;
    });
}

void BSDFApplication::toggle_color_map_selection_window()
{
    toggle_window(m_color_map_selection_window, [this]() {
        auto window = new ColorMapSelectionWindow{ this, m_color_maps };
        window->set_close_callback([this]() { toggle_color_map_selection_window(); });
        window->set_selection_callback([this](shared_ptr<ColorMap> color_map) { select_color_map(color_map); });
        auto pos = distance(m_color_maps.begin(), find(m_color_maps.begin(), m_color_maps.end(), m_bsdf_canvas->color_map()));
        window->set_selected_button(static_cast<size_t>(pos));
        window->center();
        return dynamic_cast<Window*>(window);
    });
}

void BSDFApplication::select_color_map(shared_ptr<ColorMap> color_map)
{
    m_bsdf_canvas->set_color_map(color_map);
}

int BSDFApplication::dataset_index(const shared_ptr<const Dataset> dataset) const
{
    auto pos = static_cast<size_t>(distance(m_datasets.begin(), find(m_datasets.begin(), m_datasets.end(), dataset)));
    return pos >= m_datasets.size() ? -1 : static_cast<int>(pos);
}

void BSDFApplication::select_dataset(int index, bool clamped)
{
    if (m_datasets.empty())
        return;

    if (clamped)
        index = std::max(0, std::min(static_cast<int>(m_datasets.size()-1), index));
    else if (index < 0 || index >= static_cast<int>(m_datasets.size()))
        return;

    select_dataset(m_datasets[index]);
}

void BSDFApplication::select_dataset(shared_ptr<Dataset> dataset)
{
    // de-select previously selected button
    if (dataset != m_selected_ds && m_selected_ds)
    {
        DatasetButton* old_button = corresponding_button(m_selected_ds);
        old_button->set_selected(false);
    }
    
    m_selected_ds = dataset;
    m_bsdf_canvas->select_dataset(dataset);

    reprint_footer();
    if (m_metadata_window)
    {
        toggle_metadata_window();
        toggle_metadata_window();
    }
    if (m_brdf_options_window)
        toggle_brdf_options_window();
    if (m_selected_ds)
    {
        update_selection_info_window();
        toggle_brdf_options_window();
    }

    request_layout_update();

    if (!m_selected_ds) // if no dataset is selected, we can stop there
        return;
 
    auto button = corresponding_button(m_selected_ds);
    button->set_selected(true);

    // move scroll panel if needed
    int button_abs_y = button->absolute_position()[1];
    int scroll_abs_y = m_datasets_scroll_panel->absolute_position()[1];
    int button_h = button->height();
    int scroll_h = m_datasets_scroll_panel->height();

    float scroll = m_datasets_scroll_panel->scroll();
    if (button_abs_y < scroll_abs_y)
    {
        scroll = static_cast<float>(button->position()[1]) / m_dataset_button_container->height();
    }
    else if (button_abs_y + button_h > scroll_abs_y + scroll_h)
    {
        scroll = static_cast<float>(button->position()[1]) / (m_dataset_button_container->height() - button_h);
    }
    m_datasets_scroll_panel->set_scroll(scroll);
}

void BSDFApplication::delete_dataset(shared_ptr<Dataset> dataset)
{
    int index = dataset_index(dataset);
    if (index == -1)
        return;

    // erase dataset and corresponding button
    m_dataset_button_container->remove_child(index);

    m_bsdf_canvas->remove_dataset(dataset);
    m_datasets.erase(find(m_datasets.begin(), m_datasets.end(), dataset));

    // clear focus path and drag widget pointer, since it may refer to deleted button
    m_drag_widget = nullptr;
    m_drag_active = false;
    m_focus_path.clear();

    // update selected dataset, if we just deleted the selected dataset

    if (dataset == m_selected_ds)
    {
        shared_ptr<Dataset> dataset_to_select = nullptr;
        if (index >= static_cast<int>(m_datasets.size())) --index;
        if (index >= 0)
        {
            dataset_to_select = m_datasets[index];
        }
        // Make sure no button is selected
        m_selected_ds = nullptr;
        select_dataset(dataset_to_select);
    }
    request_layout_update();
}

void BSDFApplication::add_dataset(shared_ptr<Dataset> dataset)
{
    if (!dataset) {
        throw std::invalid_argument{ "Dataset may not be null." };
    }

    string clean_name = dataset->name();
    replace(clean_name.begin(), clean_name.end(), '_', ' ');
    auto dataset_button = new DatasetButton{ m_dataset_button_container, clean_name};
    dataset_button->set_fixed_height(30);

    dataset_button->set_callback([this, dataset]() { select_dataset(dataset); });

    dataset_button->set_delete_callback([this, dataset]() {
        if (dataset->dirty())
        {
            toggle_unsaved_data_window({ dataset->name() }, [this, dataset]() { delete_dataset(dataset); });
        }
        else {
            delete_dataset(dataset);
        }
    });

    dataset_button->set_toggle_view_callback([this, dataset](bool checked) {
        int index = dataset_index(dataset);
        if (checked)    m_bsdf_canvas->add_dataset(m_datasets[index]);
        else            m_bsdf_canvas->remove_dataset(m_datasets[index]);
    });

    m_datasets.push_back(dataset);
    select_dataset(dataset);

    // by default toggle view for the new datasets
    m_bsdf_canvas->add_dataset(m_selected_ds);
}

void BSDFApplication::toggle_tool_checkbox(CheckBox* checkbox)
{
    checkbox->set_checked(!checkbox->checked());
    checkbox->callback()(checkbox->checked());
}

void BSDFApplication::toggle_view(Dataset::Views view)
{
    if (!m_selected_ds)
        return;
    bool toggle = !m_selected_ds->display_view(view);
    m_selected_ds->toggle_view(view, toggle);

    if (!m_brdf_options_window)
        return;
    m_view_toggles[view]->set_pushed(toggle);
}

DatasetButton* BSDFApplication::corresponding_button(const shared_ptr<const Dataset> dataset)
{
    int index = dataset_index(dataset);
    if (index == -1)
        return nullptr;
    return dynamic_cast<DatasetButton*>(m_dataset_button_container->child_at(index));
}

const DatasetButton* BSDFApplication::corresponding_button(const shared_ptr<const Dataset> dataset) const
{
    int index = dataset_index(dataset);
    if (index == -1)
        return nullptr;
    return dynamic_cast<DatasetButton*>(m_dataset_button_container->child_at(index));
}

void BSDFApplication::reprint_footer()
{
    m_dataset_name->set_caption             (!m_selected_ds ? "-" : m_selected_ds->name());
    m_dataset_points_count->set_caption     (!m_selected_ds ? "-" : to_string(m_selected_ds->points_count()));
    m_dataset_average_height->set_caption   (!m_selected_ds ? "-" : to_string(m_selected_ds->average_intensity()));
}

void BSDFApplication::hide_windows()
{
    m_distraction_free_mode = !m_distraction_free_mode;

    auto toggle_visibility = [this](Window* w) { if (w) w->set_visible(!m_distraction_free_mode); };

    toggle_visibility(m_tool_window);
    toggle_visibility(m_metadata_window);
    toggle_visibility(m_help_window);
    toggle_visibility(m_color_map_selection_window);
    toggle_visibility(m_selection_info_window);
    toggle_visibility(m_unsaved_data_window);
    toggle_visibility(m_brdf_options_window);
}

void BSDFApplication::try_load_dataset(const string& file_path, shared_ptr<Dataset_to_add> dataset_to_add)
{
    try {
        size_t pos = file_path.find_last_of(".");
        string extension = file_path.substr(pos+1, file_path.length());

        shared_ptr<Dataset> ds;
        if (extension == "bsdf")
        {
            ds = make_shared<BSDFDataset>(file_path);
        }
        else
        {
            ds = make_shared<StandardDataset>(file_path);
        }
        dataset_to_add->dataset = ds;
    }
    catch (const std::exception &e) {
        string error_msg = "Could not open dataset \"" + file_path + "\" : " + std::string(e.what());
        cerr << error_msg << endl;
        dataset_to_add->error_msg = error_msg;
    }
}

bool BSDFApplication::drop_event(const std::vector<std::string> & filenames) {
    open_files(filenames);
    return true;
}

TEKARI_NAMESPACE_END
