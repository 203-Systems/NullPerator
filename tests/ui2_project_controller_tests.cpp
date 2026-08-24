#include "Application/UI2/Controllers/Ui2ProjectController.h"

#include "doctest/doctest.h"

#include <array>
#include <type_traits>

namespace {

using ui2::Ui2ProjectBottomKind;
using ui2::Ui2ProjectCommandType;
using ui2::Ui2ProjectContentCursor;
using ui2::Ui2ProjectController;
using ui2::Ui2ProjectNameAction;
using ui2::Ui2ProjectRenderSelection;
using ui2::Ui2ProjectSampleAction;

} // namespace

TEST_CASE("UI2 Project controller keeps content and name actions independent") {
  Ui2ProjectController controller;
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);
  CHECK(controller.NameAction() == Ui2ProjectNameAction::New);

  controller.MoveRight();
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Save);
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);
  controller.MoveRight();
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Load);
  controller.MoveRight();
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Rename);
  controller.MoveRight();
  CHECK(controller.NameAction() == Ui2ProjectNameAction::New);
  controller.MoveLeft();
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Rename);

  controller.MoveDown();
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Tempo);
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Rename);
  CHECK(controller.Bottom().kind == Ui2ProjectBottomKind::ValueSelector);
  controller.MoveUp();
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);
  CHECK(controller.NameAction() == Ui2ProjectNameAction::Rename);
}

TEST_CASE("UI2 Project controller traverses only conceptual content rows") {
  Ui2ProjectController controller;
  constexpr std::array<Ui2ProjectContentCursor, 8> order{
      Ui2ProjectContentCursor::Name,       Ui2ProjectContentCursor::Tempo,
      Ui2ProjectContentCursor::Transpose,  Ui2ProjectContentCursor::Scale,
      Ui2ProjectContentCursor::Root,       Ui2ProjectContentCursor::Samples,
      Ui2ProjectContentCursor::Instruments,
      Ui2ProjectContentCursor::Render,
  };

  for (std::size_t index = 0; index < order.size(); ++index) {
    CHECK(controller.ContentCursor() == order[index]);
    controller.MoveDown();
  }
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);

  controller.MoveUp();
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Render);
  for (std::size_t index = order.size() - 1U; index > 0U; --index) {
    CHECK(controller.ContentCursor() == order[index]);
    controller.MoveUp();
  }
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);
}

TEST_CASE("UI2 Project Enter returns typed name commands in approved order") {
  Ui2ProjectController controller;
  constexpr std::array<Ui2ProjectCommandType, 4> commands{
      Ui2ProjectCommandType::NewProject, Ui2ProjectCommandType::SaveProject,
      Ui2ProjectCommandType::LoadProject,
      Ui2ProjectCommandType::RenameProject,
  };

  for (std::size_t index = 0; index < commands.size(); ++index) {
    const auto bottom = controller.Bottom();
    CHECK(bottom.kind == Ui2ProjectBottomKind::NameActions);
    CHECK(bottom.selectedIndex == index);
    CHECK(bottom.optionCount == commands.size());
    CHECK(bottom.selectedCommand == commands[index]);
    CHECK(controller.Enter().type == commands[index]);
    CHECK(controller.Enter().HasValue());
    controller.MoveRight();
  }
}

TEST_CASE("UI2 Project cleanup and render commands are explicit") {
  Ui2ProjectController controller;

  controller.MoveDown(); // Tempo
  CHECK_FALSE(controller.Enter().HasValue());
  controller.MoveDown(); // Transpose
  CHECK_FALSE(controller.Enter().HasValue());
  controller.MoveDown(); // Scale
  CHECK_FALSE(controller.Enter().HasValue());
  controller.MoveDown(); // Root
  CHECK_FALSE(controller.Enter().HasValue());

  controller.MoveDown(); // Samples
  CHECK(controller.Bottom().kind == Ui2ProjectBottomKind::SampleActions);
  CHECK(controller.SampleAction() == Ui2ProjectSampleAction::Browse);
  CHECK(controller.Enter().type == Ui2ProjectCommandType::BrowseSamplePool);
  controller.MoveRight();
  CHECK(controller.SampleAction() == Ui2ProjectSampleAction::RemoveUnused);
  CHECK(controller.Enter().type ==
        Ui2ProjectCommandType::RemoveUnusedSamples);
  controller.MoveLeft();
  CHECK(controller.Enter().type == Ui2ProjectCommandType::BrowseSamplePool);

  controller.MoveDown(); // Instruments cleanup
  CHECK(controller.Bottom().kind == Ui2ProjectBottomKind::CleanupAction);
  CHECK(controller.Enter().type ==
        Ui2ProjectCommandType::RemoveUnusedInstruments);
  controller.MoveRight();
  CHECK(controller.Enter().type ==
        Ui2ProjectCommandType::RemoveUnusedInstruments);

  controller.MoveDown(); // Render
  CHECK(controller.Bottom().kind == Ui2ProjectBottomKind::RenderSelector);
  CHECK(controller.RenderSelection() ==
        Ui2ProjectRenderSelection::Mixdown);
  CHECK(controller.Enter().type == Ui2ProjectCommandType::RenderMixdown);
  controller.MoveRight();
  CHECK(controller.RenderSelection() == Ui2ProjectRenderSelection::Stems);
  CHECK(controller.Bottom().selectedIndex == 1U);
  CHECK(controller.Bottom().optionCount == 2U);
  CHECK(controller.Enter().type == Ui2ProjectCommandType::RenderStems);
  controller.MoveRight();
  CHECK(controller.RenderSelection() ==
        Ui2ProjectRenderSelection::Mixdown);
}

TEST_CASE("UI2 Project controller sanitizes initial fixed-size state") {
  constexpr Ui2ProjectController controller{
      static_cast<Ui2ProjectContentCursor>(255),
      static_cast<Ui2ProjectNameAction>(255),
      static_cast<Ui2ProjectRenderSelection>(255)};
  CHECK(controller.ContentCursor() == Ui2ProjectContentCursor::Name);
  CHECK(controller.NameAction() == Ui2ProjectNameAction::New);
  CHECK(controller.RenderSelection() ==
        Ui2ProjectRenderSelection::Mixdown);
  CHECK(std::is_trivially_copyable_v<Ui2ProjectController>);
  CHECK(sizeof(Ui2ProjectController) <= 8U);
}

TEST_CASE("UI2 Project value rows preserve legacy fine and coarse steps") {
  Ui2ProjectController controller;
  controller.MoveDown();
  CHECK(controller.Adjust(TrackerAction::Right).type ==
        Ui2ProjectCommandType::AdjustTempo);
  CHECK(controller.Adjust(TrackerAction::Right).value == 1);
  CHECK(controller.Adjust(TrackerAction::Up).value == 10);

  controller.MoveDown();
  CHECK(controller.Adjust(TrackerAction::Left).type ==
        Ui2ProjectCommandType::AdjustTranspose);
  CHECK(controller.Adjust(TrackerAction::Down).value == -12);

  controller.MoveDown();
  CHECK(controller.Adjust(TrackerAction::Up).type ==
        Ui2ProjectCommandType::AdjustScale);
  CHECK(controller.Adjust(TrackerAction::Up).value == 10);

  controller.MoveDown();
  CHECK(controller.Adjust(TrackerAction::Down).type ==
        Ui2ProjectCommandType::AdjustRoot);
  CHECK(controller.Adjust(TrackerAction::Down).value == -1);
}
